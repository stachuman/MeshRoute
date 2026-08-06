#!/usr/bin/env bash
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# §B105 FEATURE-LAYER PROBE — the first automated cover `src/firmware_ui.cpp` has ever had.
#
# WHY THIS EXISTS.
#   `src/firmware_ui.cpp` is compiled by NEITHER the native suite (`test_build_src = no`) NOR the simulator (which
#   compiles `lib/core`, never `src/`). [[B104]] recorded the consequence honestly: RENDER POLICY, the §5 MAC-IDLE
#   GATE, the 2 Hz THROTTLE and the BATTERY CADENCE had **no behavioural probe at all** — only source greps. Every
#   lifecycle defect in the UI-6/UI-7 arc (§B107 a frame that never repainted, §B115 a counter off by one from its
#   first tick, §B113 a state assigned zero times) was found by a human reading code or by the owner on metal.
#   ⇒ [[B105]] made the TU host-compilable — `fw_context_pure.h` + `DeviceHal::radio()` instead of `fw_context.h`
#     -> `<RadioLib.h>` — and THIS is what that refactor was for. The accessor without this probe is dead config.
#
# ★★ IT MUST REMAIN IN THE REPOSITORY AND BE COMMITTED WITH THIS SLICE, for the reason
#    `tools/probe_board_ui/run.sh` states at the same spot: this project has already LOST a proven 33-assert scenario
#    to a session scratchpad ([[meshroute-agent-scratchpad-is-volatile]]). A recipe in a note is not a storage
#    location.
#    ⛔ CORRECTED IN PLACE 2026-08-06: this line used to read "IT IS COMMITTED AND MUST STAY COMMITTED". That was
#    NOT the case — this file is UNTRACKED, and under D4 the owner makes every commit, so nothing in this slice is
#    committed. The obligation above is real; the claimed state was not. (Same class as the ledger's §3 incident:
#    a document asserting a state that is not the case.)
#
# USAGE:  tools/probe_firmware_ui/run.sh            # probe + NEGATIVE CONTROLS (the controls run BY DEFAULT)
#         tools/probe_firmware_ui/run.sh --no-neg   # probe only — NOT a gate, use only while iterating
# ⚠ The controls run by default DELIBERATELY. The sibling probe documented its controls as "not optional" while the
#   standard command skipped them, so the reported gate never included them (QA, 2026-08-04). Same trap, same answer.
#
# ★★★ WHAT A CONTROL HAS TO BE HERE. Each one applies ONE mutation to a COPY of `src/firmware_ui.cpp` — the tempting
#     WRONG FIX, not merely a deletion — and must make the probe RED. Three ways a control can be worthless, all three
#     checked and all three have fired in this arc:
#       1. the sed matched nothing        -> the copy is byte-identical  -> reported VACUOUS, counted as a failure;
#       2. the mutant does not compile    -> the probe never ran         -> reported, counted as a failure
#          (C0 is the ONE exception: it is the BUILD control and is REQUIRED to fail compilation);
#       3. the probe still passes         -> the check does not measure the property -> counted as a failure.
# ⚠ And the tree must come out untouched: the real sources' md5 is captured before and asserted after.

set -uo pipefail
cd "$(dirname "$0")" || exit 1
ROOT=$(cd ../.. && pwd)          # ★ absolute — a relative path in a cwd-resetting shell silently measured nothing
HERE=$(pwd)                      #   once already (register B82). Never make these relative.
BOARD="$ROOT/variants/heltec_v3"
FAKES="$ROOT/tools/probe_board_ui/fakes"   # ⓘ SHARED, not forked (U1) — the second probe adds Print/millis/Serial there
FW_UI="$ROOT/src/firmware_ui.cpp"
CXX=${CXX:-g++}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# ⚠ These -D MUST mirror `[env:heltec_v3]` (platformio.ini). If they drift, the probe measures a configuration the
#   board never builds — the same vacuous-instrument failure the controls exist to catch. `-DARDUINO` is what selects
#   the REAL `console_sink.h` staged console (MR_CONSOLE=1), so §B91's report line is a line this probe can assert.
DEFS=(-DARDUINO=100 -DMR_FEAT_OLED=1 -DMR_UI_BTN_PIN=0 -DMR_UI_TEAM_CHANNEL_ID=0 -DMR_CONSOLE=1 -DMR_N_LAYERS=2)
INCS=(-I"$FAKES" -I"$BOARD" -I"$ROOT/lib/hal" -I"$ROOT/lib/core" -I"$ROOT/lib/console" -I"$ROOT/src" -I"$ROOT/lib/monocypher/src")
STD=(-std=gnu++20 -fno-exceptions -fno-rtti -O0)

# ---- the tree must not move ------------------------------------------------------------------------------------
MD5_BEFORE=$(cat "$FW_UI" "$HERE/probe_main.cpp" "$FAKES/Arduino.h" "$ROOT/src/fw_context_pure.h" \
                 "$ROOT/lib/hal/device_hal.h" | md5sum | cut -d' ' -f1)

echo "== §B105 feature-layer probe (src/firmware_ui.cpp, host-compiled) =="

# ---- support archive: lib/core + lib/hal + lib/console + monocypher, built ONCE and reused by every variant ------
# ⓘ No -Werror on this half: `lib/core/node_hashlocate.cpp:1365` carries a PRE-EXISTING -Wmisleading-indentation that
#   this slice neither introduced nor is allowed to fix (C1: refactor XOR fix). The gate for lib/ warnings is
#   `tools/warning_census.sh` on the real board envs, not this probe.
echo "-- building the support archive (lib/core + lib/hal + lib/console + monocypher) ..."
SUP_SRCS=("$ROOT"/lib/core/*.cpp "$ROOT/lib/hal/device_hal.cpp" "$ROOT/lib/hal/timer_wheel.cpp"
          "$ROOT/lib/hal/airtime_ledger.cpp" "$ROOT/lib/console/console_json.cpp" "$ROOT/lib/console/console_parse.cpp")
sup_objs=()
for s in "${SUP_SRCS[@]}"; do
  o="$OUT/$(basename "${s%.*}").o"
  "$CXX" "${STD[@]}" -Wall -Wextra "${DEFS[@]}" "${INCS[@]}" -c "$s" -o "$o" 2>/dev/null || {
      echo "SUPPORT BUILD FAILED on $s"; exit 1; }
  sup_objs+=("$o")
done
"$CXX" -std=gnu17 -O0 "${DEFS[@]}" -I"$ROOT/lib/monocypher/src" -c "$ROOT/lib/monocypher/src/monocypher.c" \
       -o "$OUT/monocypher.o" 2>/dev/null || { echo "SUPPORT BUILD FAILED on monocypher"; exit 1; }
sup_objs+=("$OUT/monocypher.o")
ar rcs "$OUT/libsupport.a" "${sup_objs[@]}" || exit 1

# probe_main is the harness; it is built ONCE, with -Werror, because nothing mutates it.
"$CXX" "${STD[@]}" -Wall -Wextra -Werror "${DEFS[@]}" "${INCS[@]}" -c "$HERE/probe_main.cpp" -o "$OUT/probe_main.o" || {
    echo "PROBE HARNESS BUILD FAILED — see above"; exit 1; }

# build_variant(src, out_binary, extra_cxxflags...) -> 0 on a successful link
build_variant() {
  local src=$1 bin=$2; shift 2
  "$CXX" "${STD[@]}" -Wall -Wextra "$@" "${DEFS[@]}" "${INCS[@]}" -c "$src" -o "$OUT/fw_ui_var.o" 2>"$OUT/build.log" \
    && "$CXX" "$OUT/probe_main.o" "$OUT/fw_ui_var.o" "$OUT/libsupport.a" -o "$bin" 2>>"$OUT/build.log"
}

# ---- THE LIVE RUN. -Werror here: the shipped TU must be clean at the same warning bar the board builds hold it to.
if ! build_variant "$FW_UI" "$OUT/probe" -Werror; then
  echo "PROBE BUILD FAILED — src/firmware_ui.cpp did not host-compile:"; sed 's/^/    /' "$OUT/build.log" | head -20
  exit 1
fi
"$OUT/probe"; rc=$?
echo "probe exit=$rc"
# ★ THE DENOMINATOR FOR THE COVERAGE ROLL-UP AT THE BOTTOM, taken from the probe itself. `PROBE_LIST=1` makes every
#   CHK print its label whether it passed or not, so "N of M checks are reddened by a control" is MEASURED here rather
#   than maintained by hand in a comment — which is exactly how the header's "20 of 25" went stale in one slice.
PROBE_LIST=1 "$OUT/probe" 2>/dev/null | sed -n 's/^  CHECK //p' | sort -u > "$OUT/all_checks.txt"
: > "$OUT/reddened.txt"

# ---------------------------------------------------------------------------------------------------------------
# NEGATIVE CONTROLS
# ---------------------------------------------------------------------------------------------------------------
n_ctl=0; n_bad=0
# ctl(label, must_build, sed-script) — must_build=yes: the mutant has to compile AND the probe has to go red.
#                                      must_build=no : the mutant has to FAIL TO COMPILE (the build IS the check).
ctl() {
  local label=$1 must_build=$2 script=$3
  sed "$script" "$FW_UI" > "$OUT/mutant.cpp"
  if cmp -s "$FW_UI" "$OUT/mutant.cpp"; then
    n_bad=$((n_bad+1)); echo "  FAIL $label — the mutation changed NOTHING, so the control is VACUOUS"; return
  fi
  if [ "$must_build" = no ]; then
    if build_variant "$OUT/mutant.cpp" "$OUT/mutant.bin"; then
      n_bad=$((n_bad+1)); echo "  FAIL $label — it still BUILDS, so the property is not what this control claims"
    else
      n_ctl=$((n_ctl+1)); echo "  ok   $label (build fails, as required)"
    fi
    return
  fi
  if ! build_variant "$OUT/mutant.cpp" "$OUT/mutant.bin"; then
    n_bad=$((n_bad+1)); echo "  FAIL $label — the mutant does not COMPILE, so the probe never ran against it:"
    sed 's/^/        /' "$OUT/build.log" | head -6; return
  fi
  if "$OUT/mutant.bin" >"$OUT/mutant.out" 2>&1; then
    n_bad=$((n_bad+1)); echo "  FAIL $label — the probe still PASSES against the mutant (the check measures nothing)"
  else
    n_ctl=$((n_ctl+1)); echo "  ok   $label -> RED ($(grep -c '^  FAIL' "$OUT/mutant.out") check(s) failed)"
    # record WHICH checks this control reddened, for the roll-up (the CHK format is "  FAIL <label padded to 64>  <expr>")
    sed -n 's/^  FAIL \(.\{1,64\}\)  .*$/\1/p' "$OUT/mutant.out" | sed 's/[[:space:]]*$//' >> "$OUT/reddened.txt"
  fi
}

if [ "${1:-}" != "--no-neg" ]; then
  echo
  echo "== negative controls (each MUST make the probe red — or, for C0, must fail to build) =="

  # C0 ★★ THE SLICE'S OWN CONTROL. This whole probe exists because [[B105]] took `fw_context.h` out of the TU. Put it
  #    back and the host build dies at `<RadioLib.h>` — which is the measurement, stated as a control so that nobody
  #    can reintroduce the include and still believe the gate is green.
  ctl "C0  restoring #include \"fw_context.h\" breaks the host build" no \
      's|#include "fw_context_pure.h"|#include "fw_context.h"|'

  # C1-C3 THE §5 MAC-IDLE GATE. Three mutations, because the predicate has three wrong answers and each is tempting:
  #   drop it entirely ("the gate already knows"), keep only the radio ("the queue is drained by fw_main anyway"),
  #   keep only the queue ("tx_busy is what the queue implies"). A ~25 ms blocking I2C frame against a
  #   `cts_to_data_gap_ms` of 5 is the reason all three are wrong.
  ctl "C1  mac_idle() -> true (no gate at all)" yes \
      's|bool mac_idle() { return !g_hal.radio().tx_busy() \&\& g_hal.txq_depth() == 0; }|bool mac_idle() { return true; }|'
  ctl "C2  mac_idle() keeps only the radio clause" yes \
      's|bool mac_idle() { return !g_hal.radio().tx_busy() \&\& g_hal.txq_depth() == 0; }|bool mac_idle() { return !g_hal.radio().tx_busy(); }|'
  ctl "C3  mac_idle() keeps only the queue clause" yes \
      's|bool mac_idle() { return !g_hal.radio().tx_busy() \&\& g_hal.txq_depth() == 0; }|bool mac_idle() { return g_hal.txq_depth() == 0; }|'

  # C10 ★★ THE OTHER HALF OF C1-C3, AND IT IS THE §W10b LESSON APPLIED HERE. C1-C3 only prove that a gate exists;
  #     a `mac_idle()` stuck at FALSE satisfies every suppression clause they check while the panel never paints
  #     again. The "paint RESUMES" assertions are what forbid that, and without this control NOTHING measured them —
  #     a check whose only control is the permissive direction measures half the property.
  ctl "C10 mac_idle() -> false (suppressed for ever; the panel never paints)" yes \
      's|bool mac_idle() { return !g_hal.radio().tx_busy() \&\& g_hal.txq_depth() == 0; }|bool mac_idle() { return false; }|'

  # C11 THE PAGE-FEEDBACK LOOP. `FrameGate` learns a frame is COMPLETE only from the board's `next_page()` verdict.
  #     Advance the page without feeding the answer back and the gate believes a frame is open for ever: no later
  #     frame ever opens, unread counters are never marked read, and every native case stays green because none of
  #     them compile this file. (`tools/probe_board_ui/run.sh` W2 pins the same wiring STRUCTURALLY; this measures it.)
  ctl "C11 next_page()'s verdict is never fed back to the gate" yes \
      's|    s_gate.on_page(mrui::next_page(), s_model, s_counters);|    (void)mrui::next_page();|'

  # C4 RENDER POLICY — THE CALLER HALF OF ONCE-PER-PAGE, i.e. exactly the cover [[B104]] recorded as lost. The
  #    `next_page` arm pages the panel WITHOUT re-drawing, so 7 of 8 pages ship blank. U8g2 clips per page rather
  #    than accumulating, which is why this is a defect and not an optimisation.
  ctl "C4  pages advance WITHOUT re-drawing the scene (7 of 8 blank)" yes \
      's|        case mrui::FrameStep::next_page:|        case mrui::FrameStep::next_page: mrui::set_power_save(false); s_gate.on_page(mrui::next_page(), s_model, s_counters); return;|'

  # C5 THE THROTTLE, as integration. `FrameGate` owns the decision and the native suite drives it; what nothing but
  #    this probe can see is whether the tick OBEYS it. A forced repaint in the `idle` arm ("the panel looks stale")
  #    keeps every native case green.
  ctl "C5  the idle arm forces a repaint, bypassing the 2 Hz throttle" yes \
      's|        case mrui::FrameStep::idle:|        case mrui::FrameStep::idle: mrui::begin_frame();|'

  # C6-C8 THE BATTERY CADENCE. Three wrong answers, and the middle one is the documented near-miss: gating on a
  #    SUCCEEDED read re-samples for ever on a board whose reader answers "unavailable" — which is every V3 built
  #    today, because `battery_sample_mv()` is hardcoded -1 until Task 9.
  ctl "C6  the ADC burst is no longer gated on a MAC-idle radio" yes \
      's|    if (!mac_idle()) return;|    if (false) return;|'
  ctl "C7  the cadence gates on SUCCEEDED instead of ATTEMPTED" yes \
      's|    if (s_batt_attempted \&\& uint32_t(now_ms - s_batt_next_ms) >= (1u << 31)) return;|    if (s_batt_mv >= 0 \&\& uint32_t(now_ms - s_batt_next_ms) >= (1u << 31)) return;|'
  ctl "C8  the 30 s period is never re-armed (sample every pass)" yes \
      's|    s_batt_next_ms   = now_ms + kBattPeriodMs;|    s_batt_next_ms   = now_ms;|'

  # C9 §B91's report channel, inverted — the classic. A dead panel then stays silent and a live one cries wolf.
  ctl "C9  the dead-panel report tests board_init() the wrong way round" yes \
      's|    if (!mrui::board_init()) mrcon.println|    if (mrui::board_init()) mrcon.println|'

  # C12 the boot hook stops bringing the panel up at all ("drop the noisy boot line"). Without this, P0 asserted
  #     something no mutation could take away — and an unbreakable check is not a check.
  ctl "C12 mr_ui_init no longer brings the panel up" yes \
      's|    if (!mrui::board_init()) mrcon.println(F("!! OLED panel did not ACK (check Vext / addr 0x3C / wiring)"));|    ;|'

  # C13-C16 ★★ §UI-9 — WHAT THE CADENCE PUTS ON THE PANEL. C6-C8 pin WHEN the ADC is read; these pin what the reading
  #   becomes. All four are plausible edits that leave every native case and every C1-C12 control green.
  #   ⓘ C15 is the RULED render policy, not a preference: `3.9V` or `--`, never a percentage (plan Task 9 Step 3,
  #     spec §3.3). It is a control rather than a comment so the ruling cannot be undone quietly.
  ctl "C13 an unavailable read ERASES the last good value" yes \
      's|    if (mv >= 0) s_batt_mv = mv;|    s_batt_mv = mv;|'
  ctl "C14 the unavailable render invents a plausible voltage instead of --" yes \
      's|    if (mv < 0) { snprintf(out, cap, "--"); return; }|    if (mv < 0) { snprintf(out, cap, "3.9V"); return; }|'
  ctl "C15 the bar renders a PERCENTAGE instead of volts (ruled out)" yes \
      's|    snprintf(out, cap, "%u.%uV", unsigned(mv / 1000), unsigned((mv % 1000) / 100));|    snprintf(out, cap, "%u%%", unsigned(mv / 42));|'
  ctl "C16 the bar hardcodes a voltage instead of reading the model" yes \
      's|    char volts\[12\]; fmt_volts(volts, sizeof volts, s.batt_mv);|    char volts[12]; fmt_volts(volts, sizeof volts, 3900);|'
fi

# ---- the tree must be exactly as we found it -------------------------------------------------------------------
MD5_AFTER=$(cat "$FW_UI" "$HERE/probe_main.cpp" "$FAKES/Arduino.h" "$ROOT/src/fw_context_pure.h" \
                "$ROOT/lib/hal/device_hal.h" | md5sum | cut -d' ' -f1)
if [ "$MD5_BEFORE" != "$MD5_AFTER" ]; then
  echo "  FAIL the probe MODIFIED a real source file ($MD5_BEFORE -> $MD5_AFTER)"; rc=1
else
  echo
  echo "sources unchanged: md5 $MD5_AFTER"
fi

# ---- ★★ THE COVERAGE ROLL-UP: which checks can a control actually break? ----------------------------------------
# A green probe with green controls still says nothing about the checks NO control touches. This prints the ratio and
# NAMES the exceptions, so an un-reddened check has to be justified in the source rather than assumed covered.
if [ -s "$OUT/all_checks.txt" ] && [ "${1:-}" != "--no-neg" ]; then
  sort -u "$OUT/reddened.txt" > "$OUT/reddened_u.txt"
  n_all=$(grep -c . "$OUT/all_checks.txt"); n_red=$(grep -c . "$OUT/reddened_u.txt")
  echo
  echo "coverage: $n_red of $n_all checks are reddened by at least one control"
  comm -23 "$OUT/all_checks.txt" "$OUT/reddened_u.txt" | sed 's/^/  (no control reddens) /'
elif [ "${1:-}" != "--no-neg" ]; then
  echo "  FAIL the check roll-up produced 0 labels — PROBE_LIST is not wired, so the ratio would be vacuous"; rc=1
fi

echo "controls: $n_ctl verified / $n_bad unusable"
[ "$n_bad" -eq 0 ] || rc=1
if [ "$rc" -eq 0 ]; then echo "PASS"; else echo "FAIL"; fi
exit $rc
