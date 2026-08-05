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

# ---------------------------------------------------------------------------------------------------------------------
# §UI-6 STRUCTURAL CHECKS. ⚠ WEAKER THAN THE PROBE, AND LABELLED AS SUCH: these read source/symbols instead of measuring
# behaviour. They exist because Task 6 moved three things OUT of host-compilable reach (src/firmware_ui.cpp includes
# fw_context.h, i.e. RadioLib and the whole device stack), and "no cover at all" is worse than a check that can at least
# turn red when the property alone is reverted. S1 is the strongest — it is a symbol-table fact, not a grep.
FW_UI="$ROOT/src/firmware_ui.cpp"
BOARD_H="$BOARD/board_ui.h"
s_pass=0; s_fail=0
schk() {  # schk(label, expr-as-command)
  if eval "$2" >/dev/null 2>&1; then s_pass=$((s_pass+1))
  else s_fail=$((s_fail+1)); echo "  FAIL $1"; fi
}
echo
echo "== §UI-6 structural checks (source/symbol level — weaker than the probe, deliberately) =="
[ -f "$FW_UI" ] || { echo "  FAIL S0 src/firmware_ui.cpp is missing"; rc=1; }

# S1 ★ THE DUPLICATE-SYMBOL TRIPWIRE. Task 6 took ownership of the three mr_ui_* hooks; UI-5 had defined them in the
#    board TU as TEMPORARY. Defining them in BOTH is a link failure on three shipped envs, and this is what catches a
#    re-add. Measured with nm on the real object, not by grepping for a comment.
"$CXX" -std=gnu++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror \
   -DMR_FEAT_OLED=1 -DMR_UI_BTN_PIN=0 \
   -I"$HERE/fakes" -I"$BOARD" -I"$ROOT/lib/hal" -I"$ROOT/lib/core" -I"$ROOT/src" \
   -c "$BOARD/board_ui.cpp" -o "$OUT/board_ui.o" 2>/dev/null
if [ -f "$OUT/board_ui.o" ]; then
  defined=$(nm --defined-only "$OUT/board_ui.o" 2>/dev/null | grep -c 'mr_ui_')
  schk "S1 board_ui.o defines NO mr_ui_* symbol (got $defined)" "[ '$defined' -eq 0 ]"
else
  s_fail=$((s_fail+1)); echo "  FAIL S1 could not compile board_ui.cpp to an object"
fi
# S2 ...and the feature layer defines all three, so fw_main's unconditional calls still link.
for h in mr_ui_init mr_ui_tick mr_ui_on_push; do
  schk "S2 src/firmware_ui.cpp defines $h" "grep -qE '^void $h\\(' '$FW_UI'"
done
# S3 THE ONCE-PER-PAGE REDRAW, caller half. Two draw_frame() call sites in the tick: one at frame start, one in the
#    page-continuation branch. Deleting either leaves 7 of 8 pages blank — the defect spec §5's corrected note names.
schk "S3 draw_frame() is called from >=2 sites in the tick (got $(grep -c 'draw_frame(s_frame_state' "$FW_UI"))" \
     "[ \"\$(grep -c 'draw_frame(s_frame_state' '$FW_UI')\" -ge 2 ]"
# S4 THE HARD BOUNDARY, both directions. board_ui.h must not include the model; firmware_ui.cpp must not name the driver.
# ⚠⚠ BOTH OF THESE FAILED ON THEIR FIRST WRITING, AND FOR THE §B77 REASON: a bare grep for the forbidden name also
#    matches the COMMENT THAT FORBIDS IT — both files state the invariant in prose, so the check was reading its own
#    documentation as a violation. ⇒ strip `//` comments first (`code_of`), then grep CODE. Same class as the anchor
#    table that matched the sentence prescribing the anchor.
code_of() { sed 's://.*::' "$1"; }
schk "S4a board_ui.h #includes no firmware_ui_model.h" \
     "! code_of '$BOARD_H' | grep -qE '^[[:space:]]*#[[:space:]]*include.*firmware_ui_model'"
schk "S4b firmware_ui.cpp CODE names no U8g2/Wire/GPIO symbol" \
     "! code_of '$FW_UI' | grep -qE 'U8g2|U8G2|\\bWire\\b|pinMode\\(|digitalRead\\(|analogRead\\('"
# S5 §B91's report channel actually exists on the caller side (the probe cannot see it).
schk "S5 firmware_ui.cpp surfaces a failed board_init()" "grep -q 'if (!mrui::board_init())' '$FW_UI'"
echo "structural: $s_pass passed / $s_fail failed / $((s_pass+s_fail)) total"
[ "$s_fail" -eq 0 ] || rc=1

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
