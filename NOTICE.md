# Third-party attributions

## RadioLib
`jgromes/RadioLib` (https://github.com/jgromes/RadioLib) — MIT licensed.
Used as the LoRa physical-layer driver for the SX1262 / SX1276 / LR11xx
chips on all firmware targets.

## rweather/Crypto
`rweather/Crypto` (https://github.com/rweather/arduinolibs) — MIT
licensed. Reserved for the §8 cryptography work (ChaCha20-Poly1305,
X25519); not yet activated.

## MeshCore
`https://github.com/ripplebiz/MeshCore` — MIT licensed
(Copyright (c) 2025 Scott Powell / rippleradios.com). We vendor its SX1262
PHY glue **byte-identical** (no edits) into `lib/meshcore/`, plus its
Seeed XIAO nRF52840 board definition into `boards/`:
- `lib/meshcore/src/helpers/radiolib/CustomSX1262.h` (`CustomSX1262 :
  RadioLib::SX1262` + the one-call `std_init()`)
- `lib/meshcore/src/helpers/radiolib/SX126xReset.h` (the SX126x reset helper)
- `boards/seeed-xiao-afruitnrf52-nrf52840.json` + `boards/nrf52840_s140_v7.ld`
- `variants/Seeed_XIAO_nRF52840/variant.{h,cpp}` (the Seeed XIAO pin map — the
  Adafruit BSP ships no XIAO variant; dir renamed to match the board JSON name)

Pinned commit + the verbatim MIT text live in `lib/meshcore/NOTICE` and
`lib/meshcore/license.txt`; re-sync with `tools/vendor_meshcore.sh`. We use
only the PHY (SX1262 driver) — NOT MeshCore's `Dispatcher`/mesh stack; the
MAC/routing is MeshRoute's own. The MeshCore project is unaffiliated with
MeshRoute; we just stand on their hardware-integration shoulders.

## doctest
`doctest/doctest` (https://github.com/doctest/doctest) — MIT licensed.
Single-header test framework for native + on-device unit tests.

---

If you contribute code derived from another open-source project, add
an entry here citing the upstream and its license. Keep the original
copyright header in the file itself.
