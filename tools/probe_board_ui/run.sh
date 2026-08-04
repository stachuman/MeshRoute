#!/usr/bin/env bash
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# §UI-5 board-canvas probe — the ONLY automated cover `variants/heltec_v3/board_ui.cpp` will ever have.
#
# WHY THIS EXISTS AND WHY IT IS NOT A `pio` ENV:
#   `board_ui.cpp` is compiled by NEITHER the native suite (`test/` is native-only and has no Arduino/U8g2) NOR the
#   simulator (which never compiles `src/` or `variants/`). So the two behaviours that matter most are unreachable by
#   every automated gate this project has:
#     • EDGE-TRIGGERED blanking — `set_power_save(1)` exactly once on the transition, never per tick. A per-tick call
#       means continuous I2C traffic from a panel that is supposed to be OFF.
#     • PAGE-CHUNKED rendering — a full 1024 B frame at 400 kHz is ~25 ms of blocking I2C, against a
#       `cts_to_data_gap_ms` of 5 and 5-8 ms turnarounds. A full-frame repaint DROPS RADIO FRAMES.
#   ⇒ this probe host-compiles the REAL `board_ui.cpp` against COUNTING SHIMS for Arduino and U8g2 (`fakes/`), so those
#   behaviours become assertable without a board.
#
# ★★ IT IS COMMITTED, DELIBERATELY, AND MUST STAY COMMITTED. It lived in a session scratchpad first; this project has
#    already LOST a proven 33-assert scenario that way (see MEMORY / `meshroute-agent-scratchpad-is-volatile`). A
#    reconstruction recipe in a note is not a storage location. Owner-ruled 2026-08-04.
#
# USAGE:  tools/probe_board_ui/run.sh            # probe + NEGATIVE CONTROLS (the controls run BY DEFAULT)
#         tools/probe_board_ui/run.sh --no-neg   # probe only -- NOT a gate, use only while iterating
# ⚠ The controls run by default DELIBERATELY: the previous version documented them as "not optional" while the
#   standard command skipped them, so the reported gate never included them (QA, 2026-08-04).
#
# ⚠ The negative controls are the point: each reverts ONE behaviour in a COPY of `board_ui.cpp` and must turn the probe
#   red. A probe that passes against a broken copy is measuring nothing — that is the failure mode this project keeps
#   finding, so the controls are not optional.

set -uo pipefail
cd "$(dirname "$0")" || exit 1
ROOT=$(cd ../.. && pwd)                 # ★ absolute — a relative path in a cwd-resetting shell silently measured
                                        #   nothing once already (register B82). Never make these relative.
HERE=$(pwd)
BOARD="$ROOT/variants/heltec_v3"
CXX=${CXX:-g++}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# ⚠ These two -D MUST mirror `[env:heltec_v3]` (`platformio.ini:221`/`:227`). If they drift, the probe measures a
#   configuration the board never builds — which is the same vacuous-instrument failure the controls exist to catch.
build() {   # build($1 = board_ui.cpp path, $2 = output binary)
  "$CXX" -std=gnu++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror \
     -DMR_FEAT_OLED=1 -DMR_UI_BTN_PIN=0 \
     -I"$HERE/fakes" -I"$BOARD" -I"$ROOT/lib/hal" -I"$ROOT/lib/core" -I"$ROOT/src" \
     "$HERE/probe_main.cpp" "$1" -o "$2" 2>&1
}

echo "== §UI-5 board-canvas probe =="
if ! build "$BOARD/board_ui.cpp" "$OUT/probe"; then
  echo "PROBE BUILD FAILED — see above"; exit 1
fi
"$OUT/probe"; rc=$?
echo "probe exit=$rc"

if [ "${1:-}" != "--no-neg" ]; then
  echo
  echo "== negative controls (each MUST fail) =="
  [ -f "$HERE/negctl.py" ] || { echo "negctl.py missing"; exit 1; }
  # ★ Pass the paths AND the compiler config, so the controls cannot drift from the probe they are controlling.
  python3 "$HERE/negctl.py" "$BOARD/board_ui.cpp" "$OUT" "$CXX" \
     -std=gnu++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror \
     -DMR_FEAT_OLED=1 -DMR_UI_BTN_PIN=0 \
     -I"$HERE/fakes" -I"$BOARD" -I"$ROOT/lib/hal" -I"$ROOT/lib/core" -I"$ROOT/src" || rc=1
fi
exit $rc
