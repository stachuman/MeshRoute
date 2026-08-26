// MeshRoute — variants/heltec_common/board_ui_traits.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Required compile-time traits for the shared Heltec page-buffer canvas. There are deliberately no fallback values:
// a missing pin, level, ADC policy or scale is a board-port error and must fail the build (C2).
#pragma once

#define MR_UI_ADC_CTRL_PROBE             1
#define MR_UI_ADC_CTRL_FIXED_ACTIVE_HIGH 2

#ifndef MR_UI_OLED_RST
#  error "MR_UI_OLED_RST is not defined — the board env must supply the OLED reset GPIO"
#endif
#ifndef MR_UI_OLED_SCL
#  error "MR_UI_OLED_SCL is not defined — the board env must supply the OLED clock GPIO"
#endif
#ifndef MR_UI_OLED_SDA
#  error "MR_UI_OLED_SDA is not defined — the board env must supply the OLED data GPIO"
#endif
#ifndef MR_UI_OLED_ADDR
#  error "MR_UI_OLED_ADDR is not defined — the board env must supply the OLED I2C address"
#endif
#ifndef MR_UI_VEXT_PIN
#  error "MR_UI_VEXT_PIN is not defined — the board env must supply the panel-power GPIO"
#endif
#ifndef MR_UI_VEXT_ON_LEVEL
#  error "MR_UI_VEXT_ON_LEVEL is not defined — the board env must supply the panel-power level"
#endif
#ifndef MR_UI_BTN_PIN
#  error "MR_UI_BTN_PIN is not defined — the board env must supply the active-low user-button GPIO"
#endif
#ifndef MR_UI_ADC_CTRL
#  error "MR_UI_ADC_CTRL is not defined — the board env must supply the battery-divider control GPIO"
#endif
#ifndef MR_UI_VBAT_READ
#  error "MR_UI_VBAT_READ is not defined — the board env must supply the battery ADC input GPIO"
#endif
#ifndef MR_UI_ADC_CTRL_STRATEGY
#  error "MR_UI_ADC_CTRL_STRATEGY is not defined — select probe or fixed-active-high"
#endif
#ifndef MR_UI_VBAT_ADC_SCALE
#  error "MR_UI_VBAT_ADC_SCALE is not defined — the board env must supply its measured ADC scale"
#endif

#if MR_UI_ADC_CTRL_STRATEGY != MR_UI_ADC_CTRL_PROBE && \
    MR_UI_ADC_CTRL_STRATEGY != MR_UI_ADC_CTRL_FIXED_ACTIVE_HIGH
#  error "MR_UI_ADC_CTRL_STRATEGY is not a supported shared-canvas strategy"
#endif

#if MR_UI_ADC_CTRL_STRATEGY == MR_UI_ADC_CTRL_PROBE
#  ifndef MR_UI_ADC_CTRL_FAILSAFE_PARK
#    error "MR_UI_ADC_CTRL_FAILSAFE_PARK is required when ADC-control polarity is probed"
#  endif
#endif
