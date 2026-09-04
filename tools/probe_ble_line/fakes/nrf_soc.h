// MeshRoute — tools/probe_ble_line/fakes/nrf_soc.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §0f PROBE SHIM — the two SoftDevice RNG SVCALLs and NRF_SUCCESS that `src/device_rng.h`'s nRF52 arm names.
// Compile-only, exactly as `nrf.h` above: `mrrng::fill()` is never called on this probe.
#pragma once
#include <stdint.h>

#define NRF_SUCCESS 0u

uint32_t sd_rand_application_vector_get(uint8_t* p_buff, uint8_t length);
uint32_t sd_rand_application_bytes_available_get(uint8_t* p_bytes_available);
