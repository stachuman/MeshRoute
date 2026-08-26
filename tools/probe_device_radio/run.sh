#!/usr/bin/env bash
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# V4 radio/profile gate. It host-compiles the REAL production device_radio.h and Heltec V4 board_rf.cpp against
# counting silicon/GPIO fakes, then checks fw_main and derived-profile wiring the native suite cannot link. Controls
# must all go RED.
#
# Usage: tools/probe_device_radio/run.sh            # live probe + all controls (the gate)
#        tools/probe_device_radio/run.sh --no-neg   # live probe only, for iteration; not a release gate
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
DEVICE="$ROOT/lib/hal/device_radio.h"
BOARD_RF="$ROOT/variants/heltec_v4/board_rf.cpp"
BOARD_RF_H="$ROOT/variants/heltec_v4/board_rf.h"
FW_MAIN="$ROOT/src/fw_main.cpp"
FW_CONFIG="$ROOT/src/firmware_config.cpp"
FW_COMMANDS="$ROOT/src/firmware_commands.cpp"
PIO_INI="$ROOT/platformio.ini"
CXX=${CXX:-g++}
CC=${CC:-gcc}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

source_hash() {
  sha256sum "$DEVICE" "$ROOT/lib/hal/iboard_rf.h" "$ROOT/lib/hal/board_rf_provider.h" "$ROOT/lib/hal/rf_capabilities.h" \
            "$BOARD_RF" "$BOARD_RF_H" "$FW_MAIN" "$FW_CONFIG" "$FW_COMMANDS" "$PIO_INI" "$HERE/probe_main.cpp" "$HERE/board_rf_probe.cpp" \
            "$HERE/structural.py" "$HERE/mutations.py" "$HERE/fakes/Arduino.h" "$HERE/fakes/driver/gpio.h" "$HERE/fakes/RadioLib.h" \
            "$HERE/fakes/helpers/radiolib/CustomSX1262.h" | sha256sum | cut -d' ' -f1
}
HASH_BEFORE=$(source_hash)

COMMON=(-std=gnu++20 -Wall -Wextra -Werror -Wno-volatile -fno-exceptions -fno-rtti
        -DARDUINO=100 -DMR_RADIO_CANARY=0
        -I"$HERE/fakes" -I"$ROOT/lib/hal" -I"$ROOT/lib/core" -I"$ROOT/lib/meshcore" -I"$ROOT/lib/monocypher/src")

echo "== V4 production device-radio probe =="
"$CC" -std=gnu17 -O0 -I"$ROOT/lib/monocypher/src" -c "$ROOT/lib/monocypher/src/monocypher.c" -o "$OUT/monocypher.o" || exit 1
"$CXX" "${COMMON[@]}" -c "$ROOT/lib/core/frame_codec.cpp" -o "$OUT/frame_codec.o" || exit 1
"$CXX" "${COMMON[@]}" -c "$ROOT/lib/core/leaf_config.cpp" -o "$OUT/leaf_config.o" || exit 1
"$CXX" "${COMMON[@]}" "$HERE/probe_main.cpp" "$OUT/frame_codec.o" "$OUT/leaf_config.o" "$OUT/monocypher.o" \
       -o "$OUT/probe" || exit 1
"$OUT/probe" || exit 1

echo "== V4 production Heltec board-RF probe =="
BOARD_FLAGS=(-DMR_BOARD_RF_FRONTEND=1 -DMR_V4_FEM_LDO_PIN=7 -DMR_V4_FEM_CSD_PIN=2
             -DMR_V4_GC_TX_EN_PIN=46 -DMR_V4_KCT_CTX_PIN=5 -DLORA_TX_POWER=10
             -DMR_DEFAULT_OUTPUT_DBM=22 -DMR_RF_OUTPUT_MIN_DBM=22 -DMR_RF_OUTPUT_MAX_DBM=22
             -DMR_RF_FREQ_MIN_MHZ=863.0 -DMR_RF_FREQ_MAX_MHZ=928.0 -DMR_RF_STRICT_ENVELOPE=1)
"$CXX" "${COMMON[@]}" "${BOARD_FLAGS[@]}" -I"$ROOT/variants/heltec_v4" \
       "$HERE/board_rf_probe.cpp" "$BOARD_RF" -o "$OUT/board-rf-probe" || exit 1
"$OUT/board-rf-probe" || exit 1

echo "== V4 production wiring/profile checks =="
python3 "$HERE/structural.py" "$DEVICE" "$FW_MAIN" "$FW_CONFIG" "$FW_COMMANDS" "$PIO_INI" || exit 1

if [ "${1:-}" != "--no-neg" ]; then
  SUPPORT="$OUT/frame_codec.o,$OUT/leaf_config.o,$OUT/monocypher.o"
  python3 "$HERE/mutations.py" "$ROOT" "$OUT" "$CXX" "$DEVICE" "$FW_MAIN" "$HERE/probe_main.cpp" "$SUPPORT" || exit 1
fi

HASH_AFTER=$(source_hash)
if [ "$HASH_BEFORE" != "$HASH_AFTER" ]; then
  echo "  FAIL the probe modified a real source or probe input"
  exit 1
fi
echo "source hash unchanged: $HASH_AFTER"
