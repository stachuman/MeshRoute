// Minimum RadioLib constants used by the real lib/hal/device_radio.h.
#pragma once

#include <cstdint>

static constexpr int16_t RADIOLIB_ERR_NONE = 0;
static constexpr uint16_t SX126X_IRQ_PREAMBLE_DETECTED = 0x0004u;
static constexpr uint16_t RADIOLIB_SX126X_IRQ_RX_DONE = 0x0002u;

// ★★ `Module` ADDED 2026-08-31 by §CUSTODY-D's `tools/probe_inbox_verbs/`, and it is ADDITIVE ONLY (no existing
//    declaration changed, so `probe_device_radio` resolves exactly as before — re-measured after this edit).
//    WHY: `src/fw_context.h:44` declares `extern Module g_mod` — the RadioLib hardware-abstraction handle the real
//    `CustomSX1262` is constructed over. The third probe compiles `src/firmware_commands.cpp`, which includes
//    `fw_context.h`, so the TYPE must exist for the declaration to parse. ⛔ Nothing in any probe CALLS a `Module`
//    method: the type is a declaration-level dependency only, which is why an empty stand-in is the honest shape
//    rather than a lie — a fake with invented behaviour would be the thing to avoid here, not a fake with none.
class Module {
public:
    Module(int = -1, int = -1, int = -1, int = -1) {}
};
