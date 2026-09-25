#!/usr/bin/env bash
# Scratch reproduction of tools/probe_firmware_ui/run.sh's build route, to LIST which checks a control reddens.
# usage: mutrun.sh <arm l2|v3> <mode new|base> <label-prefix>   (prints FAIL lines of the mutant run)
set -uo pipefail
ROOT=/home/staszek/MeshRoute
W=/tmp/claude-1001/-home-staszek-MeshRoute/7019fa4d-8324-4b18-b429-3c8de8a32587/scratchpad/mut
ARMSEL=$1 MODE=$2 LABEL=$3
CXX=g++
DEFS=(-DARDUINO=100 -DMR_FEAT_OLED=1 -DMR_UI_BTN_PIN=0 -DMR_UI_TEAM_CHANNEL_ID=0 -DMR_CONSOLE=1 -DMR_N_LAYERS=2)
LEAF_DEFS=(-DARDUINO=100 -DMR_FEAT_OLED=1 -DMR_UI_BTN_PIN=0 -DMR_UI_TEAM_CHANNEL_ID=0 -DMR_CONSOLE=1 -DMR_UI_ADC_CTRL=37 -DMR_UI_VBAT_READ=1)
INCS=(-I"$ROOT/tools/probe_board_ui/fakes" -I"$ROOT/variants/heltec_common" -I"$ROOT/lib/hal" -I"$ROOT/lib/core" -I"$ROOT/lib/console" -I"$ROOT/src" -I"$ROOT/lib/monocypher/src")
STD=(-std=gnu++20 -fno-exceptions -fno-rtti -O0)
if [ "$ARMSEL" = l2 ]; then D=("${DEFS[@]}"); else D=("${LEAF_DEFS[@]}"); fi
A=$W/$ARMSEL; mkdir -p $A
if [ ! -f $A/libsupport.a ]; then
  objs=()
  for s in "$ROOT"/lib/core/*.cpp "$ROOT/lib/hal/device_hal.cpp" "$ROOT/lib/hal/timer_wheel.cpp" "$ROOT/lib/hal/airtime_ledger.cpp" "$ROOT/lib/console/console_json.cpp" "$ROOT/lib/console/console_parse.cpp"; do
    o="$A/$(basename "${s%.*}").o"; $CXX "${STD[@]}" -Wall -Wextra "${D[@]}" "${INCS[@]}" -c "$s" -o "$o" 2>/dev/null || { echo SUPFAIL $s; exit 3; }; objs+=("$o"); done
  $CXX -std=gnu17 -O0 "${D[@]}" -I"$ROOT/lib/monocypher/src" -c "$ROOT/lib/monocypher/src/monocypher.c" -o "$A/monocypher.o" || exit 3
  ar rcs $A/libsupport.a "${objs[@]}" "$A/monocypher.o"
fi
M=$W/$ARMSEL-$MODE; mkdir -p $M
if [ "$MODE" = base ]; then
  git -C $ROOT show 4a230f4:tools/probe_firmware_ui/probe_main.cpp > $M/probe_main.cpp
  git -C $ROOT show 4a230f4:tools/probe_firmware_ui/run.sh > $M/run.sh
  git -C $ROOT show 4a230f4:src/firmware_ui.cpp > $M/fw_src.cpp
else
  cp $ROOT/tools/probe_firmware_ui/probe_main.cpp $M/probe_main.cpp
  cp $ROOT/tools/probe_firmware_ui/run.sh $M/run.sh
  cp $ROOT/src/firmware_ui.cpp $M/fw_src.cpp
fi
[ -f $M/probe_main.o ] || $CXX "${STD[@]}" -Wall -Wextra -Werror "${D[@]}" "${INCS[@]}" -c $M/probe_main.cpp -o $M/probe_main.o || exit 4
# extract the ctl call (label line + its script lines) from THAT run.sh, and replay it with a dump-only ctl
start=$(grep -n "^  ctl \"$LABEL " $M/run.sh | head -1 | cut -d: -f1)
[ -n "$start" ] || { echo "NO CTL $LABEL"; exit 5; }
end=$(awk -v s=$start 'NR>s && /^  ctl "/{print NR-1; exit} NR>s && /^  ARM=|^fi$|^  # /{print NR-1; exit}' $M/run.sh)
sed -n "${start},${end}p" $M/run.sh > $M/ctl-$LABEL.sh
FW_UI=$M/fw_src.cpp
ctl() { sed "$3" "$FW_UI" > "$M/mutant-$LABEL.cpp"; }
source $M/ctl-$LABEL.sh
cmp -s $M/fw_src.cpp $M/mutant-$LABEL.cpp && { echo "VACUOUS"; exit 6; }
if [ "$MODE" = base ]; then src=$M/mutant-$LABEL.cpp
else printf '#include "%s"\nvoid mr_probe_label_from_hash(uint32_t hash, char* out, uint8_t cap) { label_from_hash(hash, out, cap); }\n' "$M/mutant-$LABEL.cpp" > $M/wrap-$LABEL.cpp; src=$M/wrap-$LABEL.cpp; fi
$CXX "${STD[@]}" -Wall -Wextra "${D[@]}" "${INCS[@]}" -c "$src" -o $M/mut-$LABEL.o 2>$M/build-$LABEL.log && $CXX $M/probe_main.o $M/mut-$LABEL.o $A/libsupport.a -o $M/mut-$LABEL.bin 2>>$M/build-$LABEL.log || { echo "BUILD FAILED"; head -5 $M/build-$LABEL.log; exit 7; }
rc=0; bash -c '"$1"; exit $?' _ $M/mut-$LABEL.bin > $M/out-$LABEL.txt 2>&1 || rc=$?
echo "exit=$rc fails=$(grep -c '^  FAIL ' $M/out-$LABEL.txt)"
grep '^  FAIL ' $M/out-$LABEL.txt | cut -c1-150
