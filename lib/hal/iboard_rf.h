// MeshRoute — lib/hal/iboard_rf.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Optional board RF-front-end seam. Sx1262Radio owns the RadioLib lifecycle and composes one of these when a
// board has an external FEM; boards whose SX1262 connects directly to the antenna supply no instance. This stays
// device-side: neither the protocol core nor DeviceHal needs to know which physical RF path a board uses.
#pragma once

#include <cstdint>

namespace meshroute {

enum class BoardRfKind : uint8_t {
    none = 0,
    gc1109,
    kct8103l,
    unknown,
};

enum class BoardRfLnaState : uint8_t {
    not_applicable = 0,
    on,
    bypass,
};

struct BoardRfDrive {
    bool valid;
    int8_t chip_dbm;
};

// begin/tx_mode/rx_mode are fallible hardware transitions. A false result is fail-closed: Sx1262Radio must not
// proceed into the corresponding RadioLib receive/transmit operation.
struct IBoardRf {
    virtual ~IBoardRf() = default;

    virtual bool begin() = 0;
    virtual bool tx_mode() = 0;
    virtual bool rx_mode() = 0;

    virtual BoardRfKind kind() const = 0;
    virtual BoardRfLnaState lna_state() const = 0;
    virtual bool frequency_supported(double mhz) const = 0;
    virtual BoardRfDrive drive_for_output(int8_t output_dbm) const = 0;
};

inline const char* board_rf_kind_name(BoardRfKind kind) {
    switch (kind) {
        case BoardRfKind::none:      return "none";
        case BoardRfKind::gc1109:    return "gc1109";
        case BoardRfKind::kct8103l:  return "kct8103l";
        case BoardRfKind::unknown:   return "unknown";
    }
    return "?";   // -Wreturn-type only; the default-less switch keeps -Wswitch authoritative.
}

inline const char* board_rf_lna_state_name(BoardRfLnaState state) {
    switch (state) {
        case BoardRfLnaState::not_applicable: return "n/a";
        case BoardRfLnaState::on:             return "on";
        case BoardRfLnaState::bypass:         return "bypass";
    }
    return "?";   // -Wreturn-type only; the default-less switch keeps -Wswitch authoritative.
}

}  // namespace meshroute
