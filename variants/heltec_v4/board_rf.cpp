// MeshRoute — variants/heltec_v4/board_rf.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Heltec WiFi LoRa 32 V4.2/V4.3 external-FEM driver. Board identity comes only from the two-pull runtime detector;
// the build never assumes a revision. No sleep/power-off or user-adjustable LNA policy belongs to this first port.
#include "board_rf.h"
#include "board_rf_provider.h"
#include "rf_capabilities.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

#if !defined(MR_BOARD_RF_FRONTEND) || !MR_BOARD_RF_FRONTEND
#error "heltec_v4/board_rf.cpp requires MR_BOARD_RF_FRONTEND=1"
#endif

#ifndef MR_V4_FEM_LDO_PIN
#error "MR_V4_FEM_LDO_PIN is required"
#endif
#ifndef MR_V4_FEM_CSD_PIN
#error "MR_V4_FEM_CSD_PIN is required"
#endif
#ifndef MR_V4_GC_TX_EN_PIN
#error "MR_V4_GC_TX_EN_PIN is required"
#endif
#ifndef MR_V4_KCT_CTX_PIN
#error "MR_V4_KCT_CTX_PIN is required"
#endif

namespace meshroute {
namespace {

constexpr int8_t kNominalOutputDbm = 22;
constexpr int8_t kChipDriveDbm = 10;
static_assert(MR_DEFAULT_OUTPUT_DBM == kNominalOutputDbm,
              "Heltec V4 fresh/default requested output must be 22 dBm nominal conducted");
static_assert(LORA_TX_POWER == kChipDriveDbm,
              "Heltec V4 std_init and supported drive must use the safe 10 dBm SX1262 setting");

class HeltecV4BoardRf final : public IBoardRf {
public:
    bool begin() override {
        _ready = false;
        _kind = BoardRfKind::unknown;

        // Program the LDO level before releasing a stale pad hold, matching the board's glitch-free wake idiom.
        pinMode(MR_V4_FEM_LDO_PIN, OUTPUT);
        digitalWrite(MR_V4_FEM_LDO_PIN, HIGH);
        bool holds_released = true;
        // MeshCore's deep-sleep path holds the RTC-capable pins through the RTC IO domain. Release those holds with
        // the matching API; GPIO46 is not an RTC IO and retains the ordinary GPIO release.
        holds_released &= rtc_gpio_hold_dis(static_cast<gpio_num_t>(MR_V4_FEM_LDO_PIN)) == ESP_OK;
        holds_released &= rtc_gpio_hold_dis(static_cast<gpio_num_t>(MR_V4_FEM_CSD_PIN)) == ESP_OK;
        holds_released &= gpio_hold_dis(static_cast<gpio_num_t>(MR_V4_GC_TX_EN_PIN)) == ESP_OK;
        holds_released &= rtc_gpio_hold_dis(static_cast<gpio_num_t>(MR_V4_KCT_CTX_PIN)) == ESP_OK;
        delay(1);  // upstream cold-start interval before inspecting the powered FEM
        if (!holds_released) return park_unknown();

        pinMode(MR_V4_FEM_CSD_PIN, INPUT_PULLUP);
        delay(1);
        const bool up_a = digitalRead(MR_V4_FEM_CSD_PIN) == HIGH;
        delay(1);
        const bool up_b = digitalRead(MR_V4_FEM_CSD_PIN) == HIGH;

        pinMode(MR_V4_FEM_CSD_PIN, INPUT_PULLDOWN);
        delay(1);
        const bool down_a = digitalRead(MR_V4_FEM_CSD_PIN) == HIGH;
        delay(1);
        const bool down_b = digitalRead(MR_V4_FEM_CSD_PIN) == HIGH;

        _kind = classify_heltec_v4_fem(up_a, up_b, down_a, down_b);
        if (_kind == BoardRfKind::unknown) return park_unknown();

        // Park disabled between classification and Sx1262Radio's mandatory arm_rx authority. begin() itself does
        // not create a second semantic RX transition; arm_rx selects the revision-specific RX table immediately.
        pinMode(MR_V4_FEM_CSD_PIN, OUTPUT);
        digitalWrite(MR_V4_FEM_CSD_PIN, LOW);
        if (_kind == BoardRfKind::gc1109) {
            pinMode(MR_V4_GC_TX_EN_PIN, OUTPUT);
            digitalWrite(MR_V4_GC_TX_EN_PIN, LOW);
        } else {
            pinMode(MR_V4_KCT_CTX_PIN, OUTPUT);
            digitalWrite(MR_V4_KCT_CTX_PIN, LOW);
        }
        _ready = true;
        return true;
    }

    bool tx_mode() override {
        if (!_ready) return false;
        digitalWrite(MR_V4_FEM_LDO_PIN, HIGH);
        digitalWrite(MR_V4_FEM_CSD_PIN, HIGH);
        if (_kind == BoardRfKind::gc1109) {
            digitalWrite(MR_V4_GC_TX_EN_PIN, HIGH);
            return true;
        }
        if (_kind == BoardRfKind::kct8103l) {
            digitalWrite(MR_V4_KCT_CTX_PIN, HIGH);
            return true;
        }
        return false;
    }

    bool rx_mode() override {
        if (!_ready) return false;
        pinMode(MR_V4_FEM_CSD_PIN, OUTPUT);
        digitalWrite(MR_V4_FEM_LDO_PIN, HIGH);
        digitalWrite(MR_V4_FEM_CSD_PIN, HIGH);
        if (_kind == BoardRfKind::gc1109) {
            pinMode(MR_V4_GC_TX_EN_PIN, OUTPUT);
            digitalWrite(MR_V4_GC_TX_EN_PIN, LOW);
            return true;
        }
        if (_kind == BoardRfKind::kct8103l) {
            pinMode(MR_V4_KCT_CTX_PIN, OUTPUT);
            digitalWrite(MR_V4_KCT_CTX_PIN, LOW);  // approved first-build policy: receive LNA enabled
            return true;
        }
        return false;
    }

    BoardRfKind kind() const override { return _kind; }
    BoardRfLnaState lna_state() const override {
        return _kind == BoardRfKind::kct8103l ? BoardRfLnaState::on
                                              : BoardRfLnaState::not_applicable;
    }
    bool frequency_supported(double mhz) const override {
        return configured_frequency_supported(mhz);
    }
    BoardRfDrive drive_for_output(int8_t output_dbm) const override {
        return output_dbm == kNominalOutputDbm ? BoardRfDrive{true, kChipDriveDbm}
                                               : BoardRfDrive{false, 0};
    }

private:
    bool park_unknown() {
        _ready = false;
        _kind = BoardRfKind::unknown;
        pinMode(MR_V4_FEM_CSD_PIN, OUTPUT);
        digitalWrite(MR_V4_FEM_CSD_PIN, LOW);       // CSD/EN low disables either supported FEM
        pinMode(MR_V4_GC_TX_EN_PIN, OUTPUT);
        digitalWrite(MR_V4_GC_TX_EN_PIN, LOW);
        pinMode(MR_V4_KCT_CTX_PIN, OUTPUT);
        digitalWrite(MR_V4_KCT_CTX_PIN, LOW);
        return false;
    }

    BoardRfKind _kind = BoardRfKind::unknown;
    bool _ready = false;
};

}  // namespace

IBoardRf* board_rf_instance() {
    static HeltecV4BoardRf instance;
    return &instance;
}

}  // namespace meshroute
