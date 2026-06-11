// MeshRoute — src/device_rng.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Hardware RNG for the identity master seed (first boot + `regen`). nRF52840 NRF_RNG / ESP32 esp_random().
//
// REALITY SPLIT: like device_nv.h, the on-chip RNG is BENCH-VERIFIED BY THE USER — the host/native build
// never calls this (identity seeds come from scenario JSON in the sim). The host/unknown fallback returns
// zeros, which is INTENTIONALLY a degenerate seed so a mis-target is loud (every node would share id), not
// silently weak.
//
// nRF52 + SoftDevice: NRF_RNG direct register access is ILLEGAL once the SoftDevice is enabled (the SD owns
// the peripheral) — it HardFaults. So `fill` branches on `mrrng::sd_enabled()`, a flag the BLE init sets after
// Bluefruit.begin(): SD up -> draw from the SD entropy pool (sd_rand_application_vector_get); SD off (bare-metal,
// the default) -> the direct NRF_RNG path, byte-identical to before. NB we do NOT probe sd_softdevice_is_enabled()
// — that is itself an SVC and would fault when the SD is off. The `regen` path runs at RUNTIME under a live BLE
// link, so the SD branch is load-bearing, not just first-boot. (Bench-verified; the host build never calls this.)
#pragma once
#include <stdint.h>
#include <stddef.h>

#if defined(ARDUINO)
  #if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA) || defined(BOARD_XIAO_WIO_SX1262)
    #include <nrf.h>
    #include <nrf_soc.h>   // sd_rand_application_vector_get / _bytes_available_get / NRF_SUCCESS (SVCALLs; only CALLED
                           // when sd_enabled()). IWYU: the BSP already pulls this transitively, so its SVCALL
                           // -Wunused-parameter noise is pre-existing/BSP-wide, NOT from this file.
    #define MRRNG_NRF52 1
  #elif defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3)
    #include <esp_random.h>
    #include <bootloader_random.h>     // bootloader_random_enable/disable — a real entropy source w/o Wi-Fi/BT
    #define MRRNG_ESP32 1
  #endif
#endif

namespace mrrng {

#if defined(MRRNG_NRF52)
// Set true by the BLE init (after Bluefruit.begin() enables the SoftDevice); gates fill() onto the SD RNG. A
// function-local static (not a global) so the header stays self-contained. Default false = bare-metal path.
inline bool& sd_enabled() { static bool e = false; return e; }
#endif

// Fill `out[0..n)` with hardware-random bytes.
inline void fill(uint8_t* out, size_t n) {
#if defined(MRRNG_NRF52)
    if (sd_enabled()) {
        // SoftDevice owns NRF_RNG -> draw from its entropy pool. The pool can return fewer bytes than asked
        // when low, so poll bytes-available + accumulate until `n` are filled (a one-time 32-B seed draw).
        size_t got = 0;
        while (got < n) {
            uint8_t avail = 0;
            (void)sd_rand_application_bytes_available_get(&avail);
            if (avail == 0) continue;                          // pool refilling (SD tops it up in the background)
            uint8_t take = (avail < static_cast<uint8_t>(n - got)) ? avail : static_cast<uint8_t>(n - got);
            if (sd_rand_application_vector_get(out + got, take) == NRF_SUCCESS) got += take;
        }
        return;
    }
    NRF_RNG->CONFIG      = RNG_CONFIG_DERCEN_Msk;               // DERCEN bias correction — Nordic's documented
                                                               // requirement for unbiased, crypto-grade bytes
    NRF_RNG->TASKS_START = 1;
    for (size_t i = 0; i < n; ++i) {
        NRF_RNG->EVENTS_VALRDY = 0;
        while (NRF_RNG->EVENTS_VALRDY == 0) { /* spin until the next byte is ready */ }
        out[i] = static_cast<uint8_t>(NRF_RNG->VALUE);
    }
    NRF_RNG->TASKS_STOP = 1;
#elif defined(MRRNG_ESP32)
    // esp_random() is only a TRNG while an entropy source is active. A LoRa-only Heltec runs the SX1262 over
    // SPI with Wi-Fi/BT RF OFF, so esp_random() would fall back to PRNG (predictable seeds). Enable the
    // SAR-ADC entropy source for the draw, then release it. (Must NOT be paired with Wi-Fi/BT — which is off.)
    bootloader_random_enable();
    for (size_t i = 0; i < n; i += 4) {
        const uint32_t r = esp_random();                       // now a true hardware draw
        for (size_t j = 0; j < 4 && (i + j) < n; ++j) out[i + j] = static_cast<uint8_t>(r >> (8 * j));
    }
    bootloader_random_disable();
#else
    for (size_t i = 0; i < n; ++i) out[i] = 0;                 // host/unknown — degenerate-on-purpose (see header)
#endif
}

}  // namespace mrrng
