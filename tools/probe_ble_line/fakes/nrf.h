// MeshRoute — tools/probe_ble_line/fakes/nrf.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §0f PROBE SHIM — the smallest possible stand-in for Nordic's <nrf.h>. `src/device_ble.h` includes
// `src/device_rng.h` for ONE symbol (`mrrng::sd_enabled()`, the SoftDevice keystone flag), and that header's
// nRF52 arm references the RNG peripheral registers. It is never CALLED on this probe — no code path here draws
// entropy — so the registers only have to EXIST for the arm to compile.
// ⛔ Contains no intake decision and no line-length knowledge.
#pragma once
#include <stdint.h>

#define RNG_CONFIG_DERCEN_Msk (1u << 0)

typedef struct {
    volatile uint32_t CONFIG;
    volatile uint32_t TASKS_START;
    volatile uint32_t TASKS_STOP;
    volatile uint32_t EVENTS_VALRDY;
    volatile uint32_t VALUE;
} NRF_RNG_Type;

extern NRF_RNG_Type* const NRF_RNG;
