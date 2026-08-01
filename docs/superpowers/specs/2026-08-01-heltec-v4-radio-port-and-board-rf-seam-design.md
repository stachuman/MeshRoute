<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Heltec V4 radio port + the board-RF seam — design spec

*2026-08-01. Owner-approved scope: the V4 port is the vehicle for a **board-RF seam** (option (b) of the 2026-08-01 source-separation discussion) rather than a one-off. Line references code-verified today; **re-verify before acting (V1/V2)** — this tree moves several times a day.*

**Status: SPEC'D, NOT DISPATCHED.**

**Why it exists:** `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md` §10.2 named the V4 radio port a Phase B prerequisite (B-1/B-2) but deliberately did not contain it — a PA/LNA switching path and a transmit-power semantics change do not belong inside a display feature (C1). This is that spec.

---

## 0. The problem, in one paragraph

The Heltec V4 has an **external front-end module** (PA + LNA) that must be switched between transmit and receive on **every frame**. MeshCore drives it from `onBeforeTransmit()` / `onAfterTransmit()` on a per-board `Board` class (`~/MeshCore/variants/heltec_v4/HeltecV4Board.cpp`). **Our tree has no such hook**: `Sx1262Radio` drives RadioLib directly (`lib/hal/device_radio.h`), and nothing between the MAC and the silicon can run per-TX board logic. Without it a V4 transmits through a **bypassed PA with the LNA still in circuit** — the radio appears to work, badly, in a way no test in our corpus can see.

## 1. What is already right (measured 2026-08-01, do not rebuild it)

Establishing this first, because the seam is much smaller than "add a board abstraction" suggests:

| already solved | by | evidence |
|---|---|---|
| protocol ⟂ device | `lib/core` purity | **1** board `#if` in 43 files (`frame_trace.h:12`, a debug trace); the other grep hits are comments, incl. `hal.h:8` and `node.h:4` stating the rule |
| a **different radio chip** (LR1110 / T1000-E) | **`IRadio` itself** | it is a virtual interface; a `Lr1110Radio : IRadio` is a new TU, not a refactor. This is why the T1000-E assessment scoped that variant as "one job: the driver" |
| **optional** board capability with zero cost when absent | **`IRadio`'s own precedent** | `set_rx_freq` (`iradio.h:51`), `set_rx_bw` / `set_rx_cr` (`:57-58`) are **non-pure virtuals with no-op defaults** — "an IRadio that doesn't model BW/CR need not override" |
| per-board pins | `platformio.ini` build flags | already how NSS/DIO1/BUSY/SCLK reach `Module(...)` |
| a board-capability TU behind an interface | `lib/hal/mr_ui.h` + `src/board_ui.cpp` | inline no-ops when `MR_FEAT_OLED=0`, so call sites stay unconditional |

⇒ **The only missing axis is per-TX board RF control.** That is one seam, not an architecture.

## 2. Present state of the TX path (verified)

`Sx1262Radio` (`lib/hal/device_radio.h:69`) holds `CustomSX1262& _radio` and owns four transitions:

| site | what it does | FEM needs |
|---|---|---|
| `start_transmit` `:137-157` | `standby()` → set SF/BW/CR/power/preamble → `startTransmit()` | **TX mode** before `startTransmit`; **RX mode** on the `radio_error` early-return (`:150-154`, which calls `arm_rx()`) |
| `poll_tx_done` `:162-173` | TxDone edge → `finishTransmit()` → restore `_rx_sf` → `arm_rx()` | **RX mode** |
| `abort_tx` `:178-187` | watchdog recovery → `standby()` → restore SF → `arm_rx()` | **RX mode** |
| `arm_rx()` (private, `:324`) | the common continuous-RX re-arm — **8 call sites**: `:152` (TX arm failed), `:170` (TxDone), `:186` (abort), `:196` (`set_rx_sf`), `:208` (`set_rx_freq`), `:221`/`:229` (`set_rx_bw`/`set_rx_cr`), `:296` (after `poll_rx` reads a packet) | **RX mode** |
| **`begin()` `:77`** | **calls `_radio.startReceive()` DIRECTLY**, not through `arm_rx()` | **RX mode** |

★ **`arm_rx()` is the insertion point for RX mode — but it is not the *only* one, and the exception is easy to miss.** `arm_rx()`'s own comment (`:321`) says "every RX re-arm routes through here", and for *re*-arms that is true; the **initial** arm in `begin()` (`:77`) predates it and bypasses it. A FEM hooked only into `arm_rx()` would leave the V4 booting with the front end in an **undefined state until the first TX or first received packet** — intermittent, board-specific, and invisible to every automated gate we have.

⇒ **3 call sites total**: `tx_mode()` before `startTransmit`, `rx_mode()` in `arm_rx()`, and `rx_mode()` in `begin()` before the initial `startReceive()`. *(Verified 2026-08-01 by enumerating every `startReceive` in the TU; this was an open question in an earlier draft of this spec and is now closed.)*

## 3. Design — `IBoardRf`, composed not inherited

### 3.1 The interface

A new `lib/hal/iboardrf.h`:

```cpp
// The per-TX board RF seam. A board with an external front-end (PA/LNA) implements this; every other board
// supplies nothing and the radio holds a null pointer — the calls compile to a null test the branch predictor
// eats. NOT part of IRadio: a FEM is a property of the BOARD, not of the radio chip, and the same SX1262 driver
// serves boards with and without one.
struct IBoardRf {
    virtual ~IBoardRf() = default;
    virtual void tx_mode() = 0;    // PA on, LNA out of circuit — called immediately before startTransmit()
    virtual void rx_mode() = 0;    // LNA in circuit, PA idle — called on every path that ends in receive
    virtual void sleep_mode() {}   // optional: FEM off for deep sleep (uA). Default no-op.
};
```

### 3.2 Wiring

`Sx1262Radio` gains one nullable member and two call sites:

```cpp
    explicit Sx1262Radio(CustomSX1262& radio, IBoardRf* fem = nullptr) : _radio(radio), _fem(fem) {}
    …
    IBoardRf* _fem = nullptr;
```

- `start_transmit`: `if (_fem) _fem->tx_mode();` immediately before `_radio.startTransmit(...)` — **after** the `standby()` + param block, so a refused arm still leaves the FEM correct via the existing `arm_rx()` on the error path.
- `arm_rx()` (`:324`): `if (_fem) _fem->rx_mode();` before `startReceive()`. Covers all 8 re-arm paths.
- `begin()` (`:77`): the same call before the **initial** `startReceive()` — see §2. Missing this is the one placement error that would not show up until metal.

A one-line `static_assert`-style discipline for the implementer: **grep the TU for `startReceive` and `startTransmit` and prove every occurrence is preceded by a mode call.** There are three today; a future one added without a mode call is the regression this seam invites.

**Why composition, not a `Sx1262FemRadio` subclass or virtuals on `IRadio`:**

- A subclass would have to re-override `start_transmit`, `poll_tx_done`, `abort_tx` **and** reach `arm_rx()` (private), duplicating the exact sequencing that took M11/H6 several bug-fixes to get right. Duplicating it is the U1 rot.
- Hooks on `IRadio` would make the interface's *implementation* call its own virtuals (a template method) and would push a board concern into the radio-chip contract. The LR1110 driver would inherit hooks it may not need in the same places.
- Composition keeps the FEM's own complexity — including §3.3's runtime detection — entirely inside the board TU.

**★ `_fem == nullptr` is the inertness proof.** V3, XIAO nRF52 and XIAO ESP32-S3 pass nothing, so their emitted sequence is unchanged by construction. This is what makes the seam gateable independently of the V4 port (§6, slice R1).

### 3.3 The V4 implementation

`src/board_rf_heltec_v4.cpp`, compiled only under the V4 env, implementing `IBoardRf` for **two silicon revisions behind one product name**:

- `LoRaFEMControl::init()` auto-detects at boot by reading GPIO 2's default pull level — **pull-down ⇒ GC1109 (V4.2)**, **pull-up ⇒ KCT8103L (V4.3)** — and the two need different pin sequences (`~/MeshCore/variants/heltec_v4/LoRaFEMControl.cpp`).
- Pins from `~/MeshCore/variants/heltec_v4/platformio.ini`: `P_LORA_PA_POWER=7` (FEM LDO enable), GC1109 `EN=2` / `TX_EN=46`, KCT8103L `CSD=2` / `CTX=5`.
- ⚠ **Do not edit vendored files.** The MeshCore tree is a reference, not a dependency: read the method, implement it in our TU under our own author header. (Standing rule: vendor only the RadioLib-only radio headers, never edit them.)
- ⚠ The detect reads a pin that is also a FEM control line; do it **once at boot**, cache the type, and never re-probe.

### 3.4 What this does NOT do

- **No `Board` class**, no `variants/<board>/` tree. At 3 boards (5 planned) that is ahead of the need; `IRadio` + `IBoardRf` + build flags already cover the axes we have.
- **No sweep of `fw_main.cpp`'s 10 board conditionals.** They move **only as each is touched**, one refactor slice at a time (C1). This spec moves none of them.

## 4. ★★ `tx_power` means something different on V4 — and the current range is unsafe there

**This is the part that is not a refactor.**

| | V3 (and every current board) | V4 |
|---|---|---|
| `LORA_TX_POWER` | `22` = 22 dBm at the SX1262 | `10` = **22 dBm at the antenna** |
| MeshCore's own table | — | setting **22 ⇒ 28 dBm output**, prefixed with a hardware-damage warning (`~/MeshCore/docs/faq.md` §7.7) |

Our console validates `-9..22` and documents it as SX1262 dBm (`src/firmware_config.cpp:142`). On a V4 that same range silently reaches **28 dBm actual** — past what most EU868 sub-bands permit, and into the range MeshCore warns can destroy the front end. The value also flows unchanged from `cfg set tx_power` → NV → `DeviceHal::_def_power` → `IRadio::start_transmit`'s `power_dbm` → `setOutputPower`, so **nothing on that path knows the board has gain after the chip**.

### ✅ **O-V4-1 — RULED by the owner 2026-08-01: option (b). `tx_power` means DESIRED OUTPUT dBm AT THE ANTENNA.**

One meaning fleet-wide — "what leaves the antenna" — which is also the number regulations are written against. The board translates to chip drive at the last moment.

**★ The migration is a no-op for every board that exists today.** On a board with no front end, antenna dBm **is** chip dBm (we do not model trace/connector loss), so `tx_power=22` still means exactly what it meant. Only V4 — which has no env yet — gets a non-identity translation. ⇒ **R4 carries no risk to the current fleet**, and no NV migration is needed despite being a reinterpretation.

#### ★★ The translation is NOT a constant offset, and assuming one would over-deliver

MeshCore's table gives V4 **two** operating points:

| chip setting | output | implied gain |
|---|---|---|
| 10 dBm | 22 dBm | **+12** |
| 22 dBm | 28 dBm | **+6** |

The PA **compresses** — the same pattern the Station G2 rows annotate with *"1 dB compression point"*. A single `fem_gain_db` constant (which this spec proposed before the numbers were read closely) is therefore wrong in a **dangerous direction**: calibrate it at the high point (+6) and a request for 22 dBm computes chip 16, which on a curve with +12 dB of small-signal gain actually radiates ~26-28 dBm. **Over-delivery on a regulatory limit, silently.**

#### The mechanism

`IBoardRf` gains two members, both with defaults that make a FEM-less board the identity case:

```cpp
    // Highest chip drive whose MEASURED output does not exceed `want_output_dbm`. Never over-deliver: where the
    // curve is unknown, round DOWN. Default = identity: on a board with no front end, antenna dBm IS chip dBm.
    virtual int8_t chip_dbm_for_output(int8_t want_output_dbm) const { return want_output_dbm; }
    // The board's ceiling AT THE ANTENNA. Default 22 = the SX1262's own maximum.
    virtual int8_t max_output_dbm() const { return 22; }
```

V4 implements `chip_dbm_for_output` as a lookup over a **small monotonic calibration table** of `(chip_dbm → measured_output_dbm)` pairs, selecting **the highest chip value whose output is ≤ the request**. Conservative by construction: an un-tabulated request lands on the point below it, so the node transmits *at or under* what was asked, never over.

Applied at the single site that already sets power (`device_radio.h:146`):

```cpp
        if (pw > -100) {
            const int8_t chip = _fem ? _fem->chip_dbm_for_output(pw) : pw;   // null FEM -> identity -> byte-identical
            _radio.setOutputPower(clamp_sx1262(chip));                        // and still clamp to the chip's own -9..22
        }
```

**Two clamps, both required:** the requested *antenna* value against `max_output_dbm` (at the console, so the operator is refused loudly), and the derived *chip* value against the SX1262's own −9..22 (at the radio, because a table error must not reach `setOutputPower`).

#### Console side

`src/firmware_config.cpp:142` currently validates −9..22 as chip dBm. It becomes a validation against the **board's antenna range**, exposed as build flags so the console needs no radio handle:

```ini
  -DMR_TX_MIN_OUTPUT_DBM=-9     ; defaults = the SX1262's own range (identity boards)
  -DMR_TX_MAX_OUTPUT_DBM=22
```

V4 sets its own pair. ⚠ **`MR_TX_MAX_OUTPUT_DBM` and `IBoardRf::max_output_dbm()` must agree** — same board, two consumers. Assert it once in the V4 TU rather than trusting two build flags to stay in step.

⚠ **The vendor table is a STARTING POINT, not the calibration.** MeshCore's numbers are a vendor claim for a board with two silicon revisions; the entire reason for moving to antenna-dBm is that the number becomes regulatorily meaningful, and a meaningful number has to be **measured**. R4's bench step replaces the table with measured values, per revision if they differ (§8 Q6).

⚠ **R4 is its own slice** — it changes an operator-visible meaning and must not ride the FEM commit (C1).

## 5. Two more V4 deltas, both small

- **LoRa reset pin**: V4 uses **GPIO 12**; our `heltec_v3` env sets `LORA_PIN_RST=RADIOLIB_NC` (`platformio.ini:218`). A build-flag difference only — no code change.
- **`SX126X_REGISTER_PATCH=1`** (register 0x8B5, "improved RX") is set for V4 in MeshCore and absent from all our envs. ⚠ **Do not adopt it blindly on the other boards**: it is a per-board RX tuning claim, unmeasured here. Enable for V4 only, and note it as an A/B candidate for the rest rather than a fleet change.

## 6. Slices

| # | slice | gate |
|---|---|---|
| **R1** | `lib/hal/iboardrf.h` + the two `Sx1262Radio` call sites, `_fem = nullptr` everywhere. **No board supplies one yet.** | s18 md5 **EXACT** + all board envs. A null-FEM build must be byte-identical — that is the whole proof the seam is inert. |
| **R2** | `heltec_v4` / `heltec_v4_mobile` envs + pins + `SX126X_REGISTER_PATCH`; **no FEM yet** | boards build; **do not transmit on real V4 hardware after R2 and before R3** |
| **R3** | `src/board_rf_heltec_v4.cpp` — GC1109/KCT8103L runtime detect + `tx_mode`/`rx_mode`/`sleep_mode`; wire it into the V4 `Sx1262Radio` ctor | on-target; **bench-verified with a power meter or a second node's RSSI**, per M2 |
| **R4** | **O-V4-1 as ruled**: `tx_power` = antenna dBm. `IBoardRf::chip_dbm_for_output()` + `max_output_dbm()`, the console range flags, the V4 calibration table, both clamps | s18 **EXACT** (identity boards are unchanged — see §4) + all board envs + **the bench calibration below** |

R1 is the only slice that touches shared code, and its gate is byte-identity. R2/R3 are additive per-board TUs. R4 is deliberately last: it is a policy change, not a port — and although §4 shows it is a no-op for every current board, it is the slice where a mistake radiates.

**R4's bench step is not optional (M2).** Measure actual output at each table point on real V4 hardware — a power meter, or a calibrated second node's RSSI at fixed distance — and **replace the vendor numbers with the measured ones**. Record the pairs in `docs/2026-07-31-bench-test-script.md`. A calibration table that was never measured is a guess wearing a regulatory label.

## 7. Test strategy

**Native** — `IBoardRf` is a pure interface, so `test_device_hal.cpp`'s existing `MockRadio` pattern extends directly: a `MockFem` recording call order, asserting **`tx_mode` precedes every `startTransmit`** and **`rx_mode` precedes every `startReceive`** — including the **initial arm in `begin()`** (§2), the `radio_error` early-return, and the `abort_tx` watchdog path. This is the valuable half: ordering is what a FEM gets wrong, and it is cheap to test and expensive to debug on metal.

⚠ `Sx1262Radio` itself is device-only (it pulls RadioLib), so the mock must sit at the `IBoardRf` seam driven by a test double of the *sequence*, not by instantiating `Sx1262Radio` on host. If that proves awkward, the fallback is an on-target trace assertion — but try native first; the ordering is pure logic.

**s18 byte-identity** — R1's real gate. `_fem == nullptr` ⇒ no behaviour change ⇒ the stream must not move. If it does, the seam was inserted in the wrong place.

**Metal (M2 — add to `docs/2026-07-31-bench-test-script.md`)**, because no automated gate can reach any of it:

1. **The R4 calibration itself** — actual radiated output at each table point, per silicon revision if both are to hand (§8 Q6). This *is* the table; the vendor numbers are only its seed.
2. RX sensitivity sanity: V4 hears a distant node the V3 also hears (LNA actually in circuit).
3. **Both silicon revisions** if both are available — the GC1109/KCT8103L detect is a runtime branch no build can prove.
4. Deep sleep current with `sleep_mode()` — MeshCore notes the FEM drops to µA only when explicitly shut down.

## 8. Open questions for the reviewer

1. ~~O-V4-1~~ ✅ **RULED 2026-08-01: antenna dBm** (§4). Two things came out of specifying it that a reviewer should still check: (i) the V4 gain is **non-linear** (+12 at 10 dBm drive, +6 at 22), so the table-with-round-down replaces the constant-offset sketch — verify the "never exceed" selection rule is actually monotonic-safe; (ii) it is a **no-op for every existing board**, which is the claim R4's s18 gate rests on.

6. **§4 — do the two V4 silicon revisions share a power curve?** MeshCore publishes one table for "Heltec V4" while `LoRaFEMControl` detects GC1109 (V4.2) vs KCT8103L (V4.3) at runtime. Different PAs plausibly differ in gain and compression. If they do, `chip_dbm_for_output()` must select its table by the detected type — the mechanism already allows it, but nobody has measured whether it is needed. **This is a bench question, not a code-review one.**
2. ~~Is `arm_rx()` the only RX-mode site?~~ **CLOSED by enumeration during self-review, and the answer was no** — `begin():77` calls `startReceive()` directly, bypassing `arm_rx()` despite that function's own "every RX re-arm routes through here" comment (true for *re*-arms; the initial arm predates it). §2 and §3.2 now specify three sites. Kept visible because it is exactly the drift `arm_rx`'s comment invites, and the next person to add a `startReceive` will read that comment and trust it.
3. **§3.3 — is the boot-time FEM detect safe on a warm reset?** MeshCore's `init()` handles `ESP_RST_DEEPSLEEP` specially and holds RTC GPIOs across sleep. Our sleep path differs; confirm the detect is not run while the pin is held by an RTC latch.
4. **§5 — `SX126X_REGISTER_PATCH` on the other boards.** Left V4-only deliberately. If someone wants it fleet-wide it needs an A/B on metal, not a copy.
5. **Scope check:** should T1000-E's `Lr1110Radio : IRadio` be named in this spec's slice list, or stay in its own? I have kept it out — it shares no code with the FEM path and would only make this spec's gate broader.
