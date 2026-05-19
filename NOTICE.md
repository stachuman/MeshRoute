# Third-party attributions

## RadioLib
`jgromes/RadioLib` (https://github.com/jgromes/RadioLib) — MIT licensed.
Used as the LoRa physical-layer driver for the SX1262 / SX1276 / LR11xx
chips on all firmware targets.

## rweather/Crypto
`rweather/Crypto` (https://github.com/rweather/arduinolibs) — MIT
licensed. Reserved for the §8 cryptography work (ChaCha20-Poly1305,
X25519); not yet activated.

## MeshCore (planned)
`https://github.com/ripplebiz/MeshCore` — BSD-3-Clause licensed.
We plan to copy specific board-variant config snippets (pin maps for
nRF52840 + SX1262 board pairs) and HAL helpers from MeshCore once we
start the on-device bring-up. Each copied file will preserve its
original copyright header. The MeshCore project is unaffiliated with
MeshRoute; we just stand on their hardware-integration shoulders.

## doctest
`doctest/doctest` (https://github.com/doctest/doctest) — MIT licensed.
Single-header test framework for native + on-device unit tests.

---

If you contribute code derived from another open-source project, add
an entry here citing the upstream and its license. Keep the original
copyright header in the file itself.
