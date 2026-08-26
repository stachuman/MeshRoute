#!/usr/bin/env bash
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# V4-2 RF-seam gate. It host-compiles the REAL production device_radio.h against counting RadioLib/FEM fakes, then
# checks the only fw_main wiring the native suite cannot link. Negative controls run by default and must all go RED.
#
# Usage: tools/probe_device_radio/run.sh            # live probe + all controls (the gate)
#        tools/probe_device_radio/run.sh --no-neg   # live probe only, for iteration; not a release gate
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
DEVICE="$ROOT/lib/hal/device_radio.h"
FW_MAIN="$ROOT/src/fw_main.cpp"
CXX=${CXX:-g++}
CC=${CC:-gcc}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

source_hash() {
  sha256sum "$DEVICE" "$ROOT/lib/hal/iboard_rf.h" "$ROOT/lib/hal/board_rf_provider.h" \
            "$FW_MAIN" "$HERE/probe_main.cpp" "$HERE/structural.py" "$HERE/mutations.py" \
            "$HERE/fakes/Arduino.h" "$HERE/fakes/RadioLib.h" \
            "$HERE/fakes/helpers/radiolib/CustomSX1262.h" | sha256sum | cut -d' ' -f1
}
HASH_BEFORE=$(source_hash)

COMMON=(-std=gnu++20 -Wall -Wextra -Werror -Wno-volatile -fno-exceptions -fno-rtti
        -DARDUINO=100 -DMR_RADIO_CANARY=0
        -I"$HERE/fakes" -I"$ROOT/lib/hal" -I"$ROOT/lib/core" -I"$ROOT/lib/meshcore" -I"$ROOT/lib/monocypher/src")

echo "== V4-2 production device-radio probe =="
"$CC" -std=gnu17 -O0 -I"$ROOT/lib/monocypher/src" -c "$ROOT/lib/monocypher/src/monocypher.c" -o "$OUT/monocypher.o" || exit 1
"$CXX" "${COMMON[@]}" -c "$ROOT/lib/core/frame_codec.cpp" -o "$OUT/frame_codec.o" || exit 1
"$CXX" "${COMMON[@]}" -c "$ROOT/lib/core/leaf_config.cpp" -o "$OUT/leaf_config.o" || exit 1
"$CXX" "${COMMON[@]}" "$HERE/probe_main.cpp" "$OUT/frame_codec.o" "$OUT/leaf_config.o" "$OUT/monocypher.o" \
       -o "$OUT/probe" || exit 1
"$OUT/probe" || exit 1

echo "== V4-2 production wiring checks =="
python3 "$HERE/structural.py" "$DEVICE" "$FW_MAIN" || exit 1

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
