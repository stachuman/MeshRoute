// Minimum RadioLib constants used by the real lib/hal/device_radio.h.
#pragma once

#include <cstdint>

static constexpr int16_t RADIOLIB_ERR_NONE = 0;
static constexpr uint16_t SX126X_IRQ_PREAMBLE_DETECTED = 0x0004u;
static constexpr uint16_t RADIOLIB_SX126X_IRQ_RX_DONE = 0x0002u;
