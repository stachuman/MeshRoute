// MeshRoute — lib/hal/rf_capabilities.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Compile-time board RF envelope shared by command validation and the final device-radio backstop. Direct-SX1262
// boards keep the historic full RadioLib domains; a board with an external FEM narrows them in its PlatformIO env.
#pragma once

#include <cstdint>

#ifndef MR_RF_FREQ_MIN_MHZ
#define MR_RF_FREQ_MIN_MHZ 100.0
#endif
#ifndef MR_RF_FREQ_MAX_MHZ
#define MR_RF_FREQ_MAX_MHZ 1000.0
#endif
#ifndef MR_RF_FREQ_RANGE_TEXT
#define MR_RF_FREQ_RANGE_TEXT "100..1000"
#endif
#ifndef MR_RF_OUTPUT_MIN_DBM
#define MR_RF_OUTPUT_MIN_DBM -9
#endif
#ifndef MR_RF_OUTPUT_MAX_DBM
#define MR_RF_OUTPUT_MAX_DBM 22
#endif
#ifndef MR_RF_OUTPUT_RANGE_TEXT
#define MR_RF_OUTPUT_RANGE_TEXT "-9..22"
#endif
#if !defined(MR_DEFAULT_OUTPUT_DBM) && defined(LORA_TX_POWER)
#define MR_DEFAULT_OUTPUT_DBM LORA_TX_POWER
#endif

namespace meshroute {

// Existing boards preserve the historic NaN behaviour of the console parser. A board that declares a strict
// hardware envelope rejects NaN too: neither comparison succeeds, so only a real value inside the closed interval
// can reach the radio.
inline bool configured_frequency_supported(double mhz) {
#if defined(MR_RF_STRICT_ENVELOPE) && MR_RF_STRICT_ENVELOPE
    return mhz >= MR_RF_FREQ_MIN_MHZ && mhz <= MR_RF_FREQ_MAX_MHZ;
#else
    return !(mhz < MR_RF_FREQ_MIN_MHZ || mhz > MR_RF_FREQ_MAX_MHZ);
#endif
}

inline bool configured_output_supported(int output_dbm) {
    return output_dbm >= MR_RF_OUTPUT_MIN_DBM && output_dbm <= MR_RF_OUTPUT_MAX_DBM;
}

#if defined(MR_DEFAULT_OUTPUT_DBM)
inline constexpr int8_t default_output_dbm = static_cast<int8_t>(MR_DEFAULT_OUTPUT_DBM);
#endif

}  // namespace meshroute
