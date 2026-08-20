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
#    ⛔ CORRECTED IN PLACE 2026-08-06: this line used to read "IT IS COMMITTED AND MUST STAY COMMITTED". That was NOT
#    the case at the time — the file was UNTRACKED, and under D4 the owner makes every commit.
#    ✅ CORRECTED AGAIN 2026-08-13 (§UI-7D slice B, which extends this probe): the 2026-08-06 correction has itself gone
#    stale — `git ls-files` now lists `tools/probe_firmware_ui/probe_main.cpp` and `run.sh`, so the file IS tracked and
#    this slice's edits appear as ordinary modifications. The obligation was always real; both claimed STATES were
#    point-in-time. ⇒ verify with `git ls-files`, never from this comment. (The standing lesson of the ledger's §3
#    incident, arriving from the other direction: a document asserting a state that has since changed.)
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

# ⚠⚠ THERE ARE **TWO** ARMS, AND EACH MIRRORS A REAL ENV. If either set drifts from `platformio.ini`, the probe
#    measures a configuration the board never builds — the same vacuous-instrument failure the controls exist to catch.
#    `-DARDUINO` is what selects the REAL `console_sink.h` staged console (MR_CONSOLE=1) in both, so §B91's report line
#    is a line this probe can assert.
#
#   `l2`  — the LAYERED arm: the OLED/console `-D` set plus `-DMR_N_LAYERS=2`, i.e. `[env:gateway_heltec]`'s layer
#           count. ⛔ CORRECTED IN PLACE 2026-08-20 (§UI-15 slice 5, V1): this line used to read *"These -D MUST mirror
#           `[env:heltec_v3]`"*, and that was NOT true of `-DMR_N_LAYERS=2` — `[env:heltec_v3]` sets NO `MR_N_LAYERS`
#           at all, so it takes `protocol_constants.h`'s default of 1. The claim and the flag had contradicted each
#           other since the flag was added; the flag is what this arm actually measures, so the COMMENT is what moves.
#   `v3`  — ★★★★ [[B225]]'s CORRECTION, THE CHILD-ENABLED ARM: the EXACT `-D` set of `[env:heltec_v3]` — a LEAF env
#           (no `MR_N_LAYERS` ⇒ 1) with the panel (`MR_FEAT_OLED=1`) and the team plane (no `MR_PROFILE_*` ⇒
#           `MR_FEAT_TEAM=1`). It is the ONLY configuration in which `build_snapshot` publishes a provisioning child,
#           and therefore the only one in which `draw_provision_screen` (`src/firmware_ui.cpp:1143`) — real shipped
#           rendering — is reachable AT ALL. Under `l2` both children hide, so §UI-15 slice 5's three screens were
#           measured by nothing. ⓘ `MR_UI_ADC_CTRL` / `MR_UI_VBAT_READ` are carried for EXACTNESS even though the ADC
#           reader that consumes them is `variants/heltec_v3/board_ui.cpp` — faked here, and measured by the sibling
#           probe. The env's remaining flags (`-DBOARD_HELTEC_V3`, the LORA_PIN_*/SX126X_* wiring,
#           `-DMESHROUTE_NO_TELEMETRY`, `-I variants/heltec_v3`) are the RADIO's and the board TU's; `INCS` already
#           carries the include dir, and no TU in THIS link reads any of the rest.
# ⚠ THE ARMS ASSERT THEIR OWN EXPECTATIONS FROM ONE SOURCE: `probe_main.cpp` carries the matching `#if MR_N_LAYERS < 2`
#   (P7's parent-row check flips direction; P15 exists only on `v3`), exactly as the `MR_UI_BLE_ROW` arm below does.
DEFS=(-DARDUINO=100 -DMR_FEAT_OLED=1 -DMR_UI_BTN_PIN=0 -DMR_UI_TEAM_CHANNEL_ID=0 -DMR_CONSOLE=1 -DMR_N_LAYERS=2)
LEAF_DEFS=(-DARDUINO=100 -DMR_FEAT_OLED=1 -DMR_UI_BTN_PIN=0 -DMR_UI_TEAM_CHANNEL_ID=0 -DMR_CONSOLE=1
           -DMR_UI_ADC_CTRL=37 -DMR_UI_VBAT_READ=1)
INCS=(-I"$FAKES" -I"$BOARD" -I"$ROOT/lib/hal" -I"$ROOT/lib/core" -I"$ROOT/lib/console" -I"$ROOT/src" -I"$ROOT/lib/monocypher/src")
STD=(-std=gnu++20 -fno-exceptions -fno-rtti -O0)

# ---- the tree must not move ------------------------------------------------------------------------------------
# ⓘ `firmware_ui_prov.h` JOINED THE LIST WITH [[B225]]'s arm: the child-enabled build compiles the adapter's PHY
#   precondition and its verdict mapping into the binary under test, so it is now a file this probe READS.
MD5_BEFORE=$(cat "$FW_UI" "$HERE/probe_main.cpp" "$FAKES/Arduino.h" "$ROOT/src/fw_context_pure.h" \
                 "$ROOT/src/firmware_ui_prov.h" "$ROOT/lib/hal/device_hal.h" | md5sum | cut -d' ' -f1)

echo "== §B105 feature-layer probe (src/firmware_ui.cpp, host-compiled) =="

# ---- support archives: lib/core + lib/hal + lib/console + monocypher, built ONCE PER ARM and reused by every variant
# ⛔⛔ ONE ARCHIVE PER ARM IS A REQUIREMENT, NOT TIDINESS: `MR_N_LAYERS` sizes `Node::_layers[]` (`lib/core/node.h:3326`),
#     so the two arms have DIFFERENT ABIs. Linking one archive into both would put a `Node` of one layout under a
#     `firmware_ui.cpp` compiled against the other — an ODR violation whose symptom is a corrupt run, not a link error.
# ⓘ No -Werror on this half: `lib/core/node_hashlocate.cpp:1365` carries a PRE-EXISTING -Wmisleading-indentation that
#   this slice neither introduced nor is allowed to fix (C1: refactor XOR fix). The gate for lib/ warnings is
#   `tools/warning_census.sh` on the real board envs, not this probe.
SUP_SRCS=("$ROOT"/lib/core/*.cpp "$ROOT/lib/hal/device_hal.cpp" "$ROOT/lib/hal/timer_wheel.cpp"
          "$ROOT/lib/hal/airtime_ledger.cpp" "$ROOT/lib/console/console_json.cpp" "$ROOT/lib/console/console_parse.cpp")
# build_support(tag, defs...) — the archive + the harness object for one arm. ⓘ ONE body, two callers (U1).
build_support() {
  local tag=$1; shift
  local defs=("$@")
  echo "-- building the $tag support archive (lib/core + lib/hal + lib/console + monocypher) ..."
  local objs=() s o
  for s in "${SUP_SRCS[@]}"; do
    o="$OUT/$tag-$(basename "${s%.*}").o"
    "$CXX" "${STD[@]}" -Wall -Wextra "${defs[@]}" "${INCS[@]}" -c "$s" -o "$o" 2>/dev/null || {
        echo "SUPPORT BUILD FAILED on $s ($tag)"; exit 1; }
    objs+=("$o")
  done
  "$CXX" -std=gnu17 -O0 "${defs[@]}" -I"$ROOT/lib/monocypher/src" -c "$ROOT/lib/monocypher/src/monocypher.c" \
         -o "$OUT/$tag-monocypher.o" 2>/dev/null || { echo "SUPPORT BUILD FAILED on monocypher ($tag)"; exit 1; }
  objs+=("$OUT/$tag-monocypher.o")
  ar rcs "$OUT/libsupport-$tag.a" "${objs[@]}" || exit 1
  # probe_main is the harness; it is built once per arm, with -Werror, because nothing mutates it.
  "$CXX" "${STD[@]}" -Wall -Wextra -Werror "${defs[@]}" "${INCS[@]}" -c "$HERE/probe_main.cpp" \
         -o "$OUT/probe_main-$tag.o" || { echo "PROBE HARNESS BUILD FAILED ($tag) — see above"; exit 1; }
}
build_support l2 "${DEFS[@]}"
build_support v3 "${LEAF_DEFS[@]}"

# ---- the arm currently under test. `ARM` picks the archive + harness object; `ARM_DEFS` names the flag array. -------
ARM=l2; ARM_DEFS=DEFS
# build_variant(src, out_binary, extra_cxxflags...) -> 0 on a successful link
build_variant() {
  local src=$1 bin=$2; shift 2
  local -n d="$ARM_DEFS"
  "$CXX" "${STD[@]}" -Wall -Wextra "$@" "${d[@]}" "${INCS[@]}" -c "$src" -o "$OUT/fw_ui_var.o" 2>"$OUT/build.log" \
    && "$CXX" "$OUT/probe_main-$ARM.o" "$OUT/fw_ui_var.o" "$OUT/libsupport-$ARM.a" -o "$bin" 2>>"$OUT/build.log"
}

# ---- THE LIVE RUN. -Werror here: the shipped TU must be clean at the same warning bar the board builds hold it to.
if ! build_variant "$FW_UI" "$OUT/probe" -Werror; then
  echo "PROBE BUILD FAILED — src/firmware_ui.cpp did not host-compile:"; sed 's/^/    /' "$OUT/build.log" | head -20
  exit 1
fi
"$OUT/probe"; rc=$?
echo "probe exit=$rc"

# ---- ★★★★ [[B225]]: THE CHILD-ENABLED ARM — `[env:heltec_v3]`, THE ONLY BUILD THAT REACHES THE PROVISIONING SCREENS
# The live run above builds `-DMR_N_LAYERS=2`, where BOTH provisioning children hide, so `draw_provision_screen` is
# structurally unreachable there and §UI-15 slice 5's three screens had no behavioural cover at all. This arm builds
# the SAME two TUs against the leaf env's own `-D` set and runs the SAME source's P15 phase against them.
echo
echo "== [[B225]] child-enabled arm: [env:heltec_v3] (a leaf env: MR_N_LAYERS defaults to 1, MR_FEAT_TEAM=1) =="
ARM=v3; ARM_DEFS=LEAF_DEFS
if ! build_variant "$FW_UI" "$OUT/probe_v3" -Werror; then
  echo "  FAIL the child-enabled arm did not build:"; sed 's/^/    /' "$OUT/build.log" | head -20; rc=1
else
  if "$OUT/probe_v3" > "$OUT/probe_v3.out" 2>&1; then
    echo "  ok   the child-enabled arm builds and PASSES"
    grep -E "^[0-9]+ passed" "$OUT/probe_v3.out" | sed 's/^/       /'
  else
    echo "  FAIL the child-enabled arm is RED:"; grep '^  FAIL' "$OUT/probe_v3.out" | sed 's/^/    /' | head -20; rc=1
  fi
fi
ARM=l2; ARM_DEFS=DEFS

# ---- ★★★ §UI-14: THE SECOND ARM OF SPEC §3.6.2's CONDITIONAL `BLE mode` ROW -------------------------------------
# The row is ABSENT when the UI-12 transport is not compiled — which is EVERY env in this tree — so the live run above
# measures only one half of the condition. ⛔ A conditional tested in one arm is a conditional whose other arm has
# never run. This builds BOTH TUs with `-DMR_UI_BLE_ROW=1` (the same flag the env that lands a transport will set) and
# requires the probe to pass THERE TOO; `probe_main.cpp` carries the matching `#if`, so one source asserts both arms.
echo
echo "== §UI-14 second arm: the BLE row's condition MET (-DMR_UI_BLE_ROW=1) =="
if "$CXX" "${STD[@]}" -Wall -Wextra -Werror -DMR_UI_BLE_ROW=1 "${DEFS[@]}" "${INCS[@]}" \
        -c "$HERE/probe_main.cpp" -o "$OUT/probe_main_ble.o" 2>"$OUT/ble.log" \
   && "$CXX" "${STD[@]}" -Wall -Wextra -Werror -DMR_UI_BLE_ROW=1 "${DEFS[@]}" "${INCS[@]}" \
        -c "$FW_UI" -o "$OUT/fw_ui_ble.o" 2>>"$OUT/ble.log" \
   && "$CXX" "$OUT/probe_main_ble.o" "$OUT/fw_ui_ble.o" "$OUT/libsupport-l2.a" -o "$OUT/probe_ble" 2>>"$OUT/ble.log"; then
  if "$OUT/probe_ble" > "$OUT/probe_ble.out" 2>&1; then
    echo "  ok   the BLE-row arm builds and PASSES ($(grep -c '' "$OUT/probe_ble.out") lines)"
    grep -E "^[0-9]+ passed" "$OUT/probe_ble.out" | sed 's/^/       /'
  else
    echo "  FAIL the BLE-row arm is RED:"; grep '^  FAIL' "$OUT/probe_ble.out" | sed 's/^/    /' | head -10; rc=1
  fi
else
  echo "  FAIL the BLE-row arm did not build:"; sed 's/^/    /' "$OUT/ble.log" | head -10; rc=1
fi
# ★ THE DENOMINATOR FOR THE COVERAGE ROLL-UP AT THE BOTTOM, taken from the probe itself. `PROBE_LIST=1` makes every
#   CHK print its label whether it passed or not, so "N of M checks are reddened by a control" is MEASURED here rather
#   than maintained by hand in a comment — which is exactly how the header's "20 of 25" went stale in one slice.
# ⛔ ONE DENOMINATOR **PER ARM**, and mixing them would be dishonest in both directions: the two arms run different
#   check SETS (P7's parent-row check flips, P15 exists only on `v3`), so a shared ratio would count a `v3` label
#   against the `l2` list and inflate the figure that is supposed to name the UNCOVERED checks.
PROBE_LIST=1 "$OUT/probe"    2>/dev/null | sed -n 's/^  CHECK //p' | sort -u > "$OUT/all_checks-l2.txt"
PROBE_LIST=1 "$OUT/probe_v3" 2>/dev/null | sed -n 's/^  CHECK //p' | sort -u > "$OUT/all_checks-v3.txt"
: > "$OUT/reddened-l2.txt"; : > "$OUT/reddened-v3.txt"

# ---------------------------------------------------------------------------------------------------------------
# ★★★★ [[B227]] — A CONTROL LABEL IS **DATA**, AND THIS RUNNER PROVES IT RATHER THAN PROMISING IT
# ---------------------------------------------------------------------------------------------------------------
# MEASURED, not hygiene. L19's label carried `` `/mrcfg` `` inside DOUBLE quotes, so the shell ran `/mrcfg` as a
# COMMAND SUBSTITUTION while building the argument — before `ctl` was even entered. The gate printed
# `run.sh: line 730: /mrcfg: No such file or directory` on stderr, emptied that part of the label, and STILL REPORTED
# PASS. ⇒ two halves, because neither can do the other's job:
#   · THE AUDIT below forbids a backtick or a `$` substitution in any `ctl` label IN THIS FILE. That is where the
#     expansion actually happens, and ⛔ no printer can undo an expansion that already occurred at the call site;
#   · THE PRINTER (`printf '%s'` of a variable, ⛔ never `eval`, and ⛔ never `echo`) is what makes a label come out
#     VERBATIM once it IS inside `ctl`. ⓘ STATED EXACTLY, because overclaiming here would be the same defect wearing
#     the other hat: `echo "$label"` did NOT cause this bug and does not re-run backticks — but it DOES interpret a
#     leading `-n`/`-e` and, under `xpg_echo`, backslash escapes, so a label is one `\n` away from a mangled line.
#     `printf '%s'` has no such arm at all.
# ⚠ THE AUDIT READS THIS FILE'S OWN TEXT, which is the only thing that can speak for labels that do not exist yet.
#   Every `ctl` call in this file is one line of the shape `ctl "<label>" yes|no \`, so the label extracts exactly.
# ⛔ VIA `$HERE`, ⛔ NEVER VIA A BARE `$0`: this script has already `cd`-ed to its own directory, so a `$0` given
#   relative to the caller's cwd would name nothing and the audit would silently measure ZERO labels ([[B82]]).
B227_SELF="$HERE/$(basename "$0")"
echo
echo "== [[B227]] every control label is inert data =="
b227_labels=$(sed -n 's/^[[:space:]]*ctl "\(.*\)" \(yes\|no\) \\$/\1/p' "$B227_SELF")
b227_hits=$(printf '%s\n' "$b227_labels" | grep -nE '`|\$' || true)
b227_n=$(printf '%s' "$b227_labels" | grep -c '')
b227_calls=$(grep -c '^[[:space:]]*ctl "' "$B227_SELF")
# ⛔ THE EXTRACTOR MUST HAVE SEEN **EVERY** CALL. A `ctl` line written in some other shape would be silently skipped,
#   and an audit that read NONE of the labels would still report "ok" — the instrument-that-cannot-fail shape this
#   whole file is built against. ⇒ the two counts are compared, so a new call shape fails the gate rather than
#   escaping it. ⓘ The figure is COUNTED, never written down here: that is how the header's "20 of 25" went stale.
if [ "$b227_n" != "$b227_calls" ]; then
  echo "  FAIL the label extractor read $b227_n of $b227_calls ctl calls — the audit below covers only part of them"
  rc=1
elif [ -n "$b227_hits" ]; then
  echo "  FAIL a ctl label carries a shell substitution — it EXECUTES before ctl ever sees it:"
  printf '%s\n' "$b227_hits" | sed 's/^/    /'; rc=1
else
  echo "  ok   none of the $b227_n ctl labels (all $b227_calls calls) contains a backtick or a \$-substitution"
fi
# ⚠ AND THE VACUITY GUARD FOR IT (§T3 P6's rule, one section over): a detector that cannot FIND anything proves
#   nothing. The same expression is run over a line that DOES carry the defect and is required to match.
if printf '%s\n' 'ctl "X the session guard is dropped (a `/mrcfg` read)" yes \' \
     | sed -n 's/^[[:space:]]*ctl "\(.*\)" \(yes\|no\) \\$/\1/p' | grep -qE '`|\$'; then
  echo "  ok   ...and the detector DOES fire on a label that carries one"
else
  echo "  FAIL the detector matches nothing at all — the check above proved nothing"; rc=1
fi
# ⓘ THE PRINTER's OWN PROOF. The label is assigned in SINGLE quotes, so nothing is expanded at the assignment, and it
#   is then printed through the exact `printf '%s'` form `ctl` uses. It must come back BYTE-FOR-BYTE — backticks and
#   `$HOME` included — with NOTHING on stderr, which is the half of the defect that WAS visible: the original ran
#   `/mrcfg` and wrote `No such file or directory` to a stream nothing was reading.
# ⚠ THE EXPECTED TEXT IS SPELLED OUT A SECOND TIME rather than compared against `$b227_probe`: holding a variable
#   against itself would pass however mangled it got, which is the tautology this whole block is about.
b227_probe='a `/mrcfg` read and a $HOME that is not mine'
b227_seen=$(printf '%s' "$b227_probe" 2>"$OUT/b227.err")
if [ "$b227_seen" = 'a `/mrcfg` read and a $HOME that is not mine' ] && [ ! -s "$OUT/b227.err" ]; then
  echo "  ok   ...and a backtick/\$-bearing label prints verbatim, executing nothing"
else
  echo "  FAIL a label with shell metacharacters did not survive the printer: [$b227_seen]"
  sed 's/^/    /' "$OUT/b227.err"; rc=1
fi

# ---------------------------------------------------------------------------------------------------------------
# NEGATIVE CONTROLS
# ---------------------------------------------------------------------------------------------------------------
n_ctl=0; n_bad=0
# ctl(label, must_build, sed-script) — must_build=yes: the mutant has to compile AND the probe has to go red.
#                                      must_build=no : the mutant has to FAIL TO COMPILE (the build IS the check).
# ⛔ THE LABEL IS PRINTED WITH `printf '%s'` AND NEVER INTERPOLATED INTO A COMMAND — see the [[B227]] block above.
ctl() {
  local label=$1 must_build=$2 script=$3
  sed "$script" "$FW_UI" > "$OUT/mutant.cpp"
  if cmp -s "$FW_UI" "$OUT/mutant.cpp"; then
    n_bad=$((n_bad+1))
    printf '  FAIL %s — the mutation changed NOTHING, so the control is VACUOUS\n' "$label"; return
  fi
  if [ "$must_build" = no ]; then
    if build_variant "$OUT/mutant.cpp" "$OUT/mutant.bin"; then
      n_bad=$((n_bad+1))
      printf '  FAIL %s — it still BUILDS, so the property is not what this control claims\n' "$label"
    else
      n_ctl=$((n_ctl+1)); printf '  ok   %s (build fails, as required)\n' "$label"
    fi
    return
  fi
  if ! build_variant "$OUT/mutant.cpp" "$OUT/mutant.bin"; then
    n_bad=$((n_bad+1))
    printf '  FAIL %s — the mutant does not COMPILE, so the probe never ran against it:\n' "$label"
    sed 's/^/        /' "$OUT/build.log" | head -6; return
  fi
  if "$OUT/mutant.bin" >"$OUT/mutant.out" 2>&1; then
    n_bad=$((n_bad+1))
    printf '  FAIL %s — the probe still PASSES against the mutant (the check measures nothing)\n' "$label"
  else
    n_ctl=$((n_ctl+1))
    printf '  ok   %s -> RED (%s check(s) failed)\n' "$label" "$(grep -c '^  FAIL' "$OUT/mutant.out")"
    # record WHICH checks this control reddened, for the roll-up (the CHK format is "  FAIL <label padded to 64>  <expr>")
    # ⓘ INTO THE CURRENT ARM's file: a control mutates ONE build, so its evidence belongs to that arm's ratio.
    sed -n 's/^  FAIL \(.\{1,64\}\)  .*$/\1/p' "$OUT/mutant.out" | sed 's/[[:space:]]*$//' >> "$OUT/reddened-$ARM.txt"
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
  # ⛔⛔ C14-C16 RETARGETED BY §CHROME-3, AND THE REASON IS RECORDED RATHER THAN THE CONTROLS QUIETLY REWRITTEN. All
  #   three used to mutate `fmt_volts`, which this slice DELETES: `mrui::ui_fmt_batt` is the same formatter expressed
  #   over decivolts, in a pure header, with a width guard `fmt_volts` never had, and a native case asserts the exact
  #   bytes of both its branches. ⇒ a sed for the old lines would have matched NOTHING and been reported VACUOUS —
  #   §CHROME-1's re-gate found exactly that failure mode ("a mutation whose target line has MOVED measures nothing"),
  #   which is why the whole battery is re-run rather than only the new entries.
  # ★ The three wrong answers are unchanged in SUBSTANCE and now land on the strip's battery slot: invent a plausible
  #   token · render a percentage · read a constant instead of the frozen chrome.
  ctl "C14 the battery token is invented instead of formatted (a plausible voltage for --)" yes \
      's|    mrui::ui_fmt_batt(tok, sizeof tok, c.batt_dv);|    snprintf(tok, sizeof tok, "3.9V");|'
  ctl "C15 the strip renders a PERCENTAGE instead of volts (ruled out)" yes \
      's|    mrui::ui_fmt_batt(tok, sizeof tok, c.batt_dv);|    snprintf(tok, sizeof tok, "%u%%", unsigned(c.batt_dv));|'
  ctl "C16 the strip hardcodes a voltage instead of reading the frozen chrome" yes \
      's|    mrui::ui_fmt_batt(tok, sizeof tok, c.batt_dv);|    mrui::ui_fmt_batt(tok, sizeof tok, 39);|'

  # C17-C26 ★★★ §UI-7D slice B — THE DETAIL/DELETE MODAL'S DEVICE HALF. The model's half is under the native gate
  #   (32 mutations); these ten are the steps NO native case can reach, and the two identity ones are the shape
  #   [[B133]] already produced once: the DM and channel seq spaces are INDEPENDENT, so a lookup or an erase that drops
  #   the KIND deletes the other store's record with the same number. The probe records DM 1/2 and channel 1/2 for
  #   exactly that reason.
  ctl "C17 the record lookup matches seq only (the kind clause dropped)" yes \
      's|    if (e.kind != c->kind \|\| e.seq != c->seq) return true;|    if (e.seq != c->seq) return true;|'
  ctl "C18 the erase targets the DM store unconditionally (B133's shape)" yes \
      's|g_node.inbox().erase(rq.kind, rq.seq)|g_node.inbox().erase(mrui::InboxKind::dm, rq.seq)|'
  ctl "C19 the preview row carries msg_id where the identity should be" yes \
      's|^    r.seq        = e.seq;|    r.seq        = e.msg_id;|'
  ctl "C20 the preview row hardcodes the kind (every row reads DM)" yes \
      's|^    r.kind       = e.kind;|    r.kind       = MESHROUTE_NS::InboxKind::dm;|'
  # ⛔ C21 IS THE HARD REQUIREMENT: "a visual disappearance without durable success is forbidden" (spec §3.5). Report
  #    every erase as a success and the modal closes while the record is still there — the one failure this feature must
  #    not have. P6g reaches it because it evicts the open record out of band, so the confirmed erase really does come
  #    back `not_found`.
  ctl "C21 every erase is reported as a SUCCESS regardless of the outcome" yes \
      's|    s_model.on_inbox_erased(rq.kind, rq.seq, g_node.inbox().erase(rq.kind, rq.seq));|    (void)g_node.inbox().erase(rq.kind, rq.seq); s_model.on_inbox_erased(rq.kind, rq.seq, MESHROUTE_NS::InboxEraseResult::erased);|'
  # C27 the mirror of C18: the erase hits the CHANNEL store whatever the record was.
  ctl "C27 the erase targets the CHANNEL store unconditionally" yes \
      's|g_node.inbox().erase(rq.kind, rq.seq));|g_node.inbox().erase(mrui::InboxKind::channel, rq.seq));|'
  # C28 the switch arms crossed — OPENING a record deletes it. A copy-paste away, and the negative-space checks
  #     ("opening DELETED NOTHING") are what see it.
  ctl "C28 opening a record DELETES it (the switch arms crossed)" yes \
      's|        case mrui::InboxWhat::open:  ui_open_inbox_detail(rq, now_ms); break;|        case mrui::InboxWhat::open:  ui_erase_inbox_record(rq); break;|'
  ctl "C22 the tick never serves the inbox request (the modal cannot open)" yes \
      's|    ui_service_inbox_request(now_ms);|    ;|'
  ctl "C23 the body length is left for the model to measure (the strlen shape)" yes \
      's|s_model.on_inbox_opened(e.kind, e.seq, e.origin, e.channel_id, e.body, e.body_len, c->now_ms);|s_model.on_inbox_opened(e.kind, e.seq, e.origin, e.channel_id, e.body, 0, c->now_ms);|'
  ctl "C24 draw_frame stops giving the modal the body" yes \
      's|    if (st.detail != mrui::InboxModal::closed) { draw_inbox_detail(st); return; }|    ;|'
  # C25-C26 the REFUSAL's two halves on the list screen — the message, and the suppressed highlight (B64's rule
  #   one plane over: a `>` beside a record the model has refused to act on is the mis-delete in display form).
  ctl "C25 the MESSAGE GONE refusal row is dropped from the list" yes \
      's|    if (st.inbox_pick_gone) body_text(kBodyRows - 1, "MESSAGE GONE");|    ;|'
  # C29-C36 ★★★ §UI-14 — THE SETTINGS SCREEN's DEVICE HALF. The model's half is under the native gate; these are the
  #   steps NO native case can reach, and C31 is THE named one: §3.6.1 forbade the marker from being `UiState::dirty`
  #   IN ADVANCE, and this file is where both are in scope at once.
  ctl "C29 the SETTINGS screen is never drawn (the switch arm dropped)" yes \
      's|        case mrui::Screen::settings: draw_settings_screen(st, s, c);  break;   // §UI-14|        case mrui::Screen::settings: break;|'
  ctl "C30 the model is never given the config service (the menu is inert)" yes \
      's|    s_model.attach_config(s_cfg);|    ;|'
  # ⛔⛔ C31 IS THE DEFECT §3.6.1 NAMED BEFORE IT COULD BE WRITTEN: `UiState::dirty` means "a repaint is owed" and is
  #    read three lines from the marker. Rendering it AS the marker leaves `CFG* UNSAVED` on a panel with nothing
  #    unsaved — P7c's "the marker is GONE once it is durable" is what sees it.
  ctl "C31 the draft marker renders UiState::dirty instead of config_unsaved()" yes \
      's|    v.unsaved  = s_cfg.config_unsaved();|    v.unsaved  = s_model.state().dirty;|'
  ctl "C32 the value row shows a default instead of the DRAFT" yes \
      's|    v.draft    = s_cfg.draft();|    v.draft    = mrfw::CfgValues{};|'
  # ⛔⛔ C33 RETARGETED BY §CHROME-4 / design §6.1, AND THE RETARGETING IS RECORDED RATHER THAN THE CONTROL DELETED.
  #   It used to mutate `if (marker[0]) { snprintf(l, …, "STATUS %s", marker); …}` — the STATUS TITLE's draft marker,
  #   which design §6 REMOVES ("the redundant `CFG* UNSAVED` / `CFG! RELOAD` decoration is removed from the STATUS
  #   title. The rail makes the state visible from every ordinary screen"). ⇒ a sed for that line would now match
  #   NOTHING and be reported VACUOUS. ★ THE FACT IT GUARDED DID NOT GO AWAY — it MOVED, onto the SETTINGS rail
  #   badge — so the control moves with it: make the badge blind and the configuration state becomes invisible from
  #   every ordinary screen, which is exactly what the old control described one presentation earlier.
  ctl "C33 the SETTINGS badge always draws the clean gear (the state becomes invisible)" yes \
      's|        case mrui::CfgBadge::unsaved:  return mrui::icons::kIconSettingsUnsaved;|        case mrui::CfgBadge::unsaved:  return mrui::icons::kIconSettings;|'
  ctl "C34 the editor is indistinguishable from the browsing row (no bracket)" yes \
      's|            if (ed) snprintf(l, sizeof l, "%c%-8s\[%s\]", here ? '"'"'>'"'"' : '"'"' '"'"', mrui::settings_row_label(r), v);|            if (false) { }|'
  ctl "C35 RESTART NEEDED never reaches STATUS (the reboot fact is dropped)" yes \
      's|    if (c.reboot) { body_text(4, mrui::kCfgRestartText); return; }|    ;|'
  # ⛔ C36 is the CONDITIONAL ROW's own control: rendering it unconditionally offers a setting this build cannot act on.
  ctl "C36 the BLE row is rendered unconditionally (the transport condition ignored)" yes \
      's|    s.ble_row    = (MR_UI_BLE_ROW != 0);|    s.ble_row    = true;|'

  # C37-C40 ★★★ §UI-14 follow-up — `mr_ui_on_config_saved`, the IMMEDIATE conflict notification §3.6.1 requires. The
  #   hook's four obligations each have a wrong answer, and C38 is the one that would ship green everywhere else: the
  #   latch raised WITHOUT a repaint is true and invisible, because `FrameGate::step` returns `idle` on a clean model.
  ctl "C37 the hook never tells the service (the notification is dropped)" yes \
      's|    s_cfg.note_external_write(b);|    (void)b;|'
  # ⛔⛔ C38 RETARGETED BY §CHROME-3, AND THE MEASUREMENT BEHIND IT IS RECORDED RATHER THAN THE CONTROL DELETED.
  #   It used to drop the hook's own `mark_dirty` alone, and that made the probe RED because nothing else asked for a
  #   repaint. It no longer does: §8.3's per-tick invalidation watches the CONFIGURATION BADGE, whose three inputs are
  #   the same three predicates the STATUS title already renders — so the panel now updates on the next tick even with
  #   the hook's own request removed. ⇒ the property "the latch is raised and NOTHING repaints" now needs BOTH writers
  #   dropped, which is the defect this control always described. ⚠ Stated plainly: the IMMEDIACY of the hook's own
  #   request is no longer separately attributable in this probe, because two independent mechanisms deliver it.
  ctl "C38 the latch is raised and NOTHING asks for a repaint (INVISIBLE)" yes \
      's|    if (s_cfg.conflict() != was) s_model.mark_dirty();|    (void)was;|
       s|    (void)mrui::ui_chrome_invalidate(s_model, live_chrome, s_frame_chrome);|    (void)live_chrome;|'
  ctl "C39 the is_open() guard is dropped (flash is read on every cfg set)" yes \
      's|    if (!s_cfg.is_open()) return;|    ;|'
  ctl "C40 an unreadable record is treated as a conflict (a latch from no evidence)" yes \
      's|    if (!mrfw::device_cfg_store().load(b)) return;|    (void)mrfw::device_cfg_store().load(b);|'

  # C41-C44 ★★★ §T3 — THE THREE CHANGED STRINGS AND THE TWO NEW STATES. Each control is the TEMPTING WRONG ANSWER,
  #   not a deletion: C41 puts `SENT` back on core acceptance (the §B69 false confirmation this slice removes), C42
  #   makes the EARNED state say the unearned word, C43 restores `SENT, no relay` on a state that never established
  #   any airing, and C44 collapses the two DM states so the promotion is invisible.
  # ⚠ C41/C42 are DIRECTIONAL OPPOSITES on purpose: with only one of them a renderer that printed the SAME string for
  #   both states would still redden something, and the pair is what proves the two states are distinguishable.
  ctl "C41 ChanState::waiting says SENT again (the pre-T3 false confirmation)" yes \
      's|case mrui::ChanState::waiting:    body_text(1, "QUEUED");|case mrui::ChanState::waiting:    body_text(1, "SENT, waiting");|'
  ctl "C42 ChanState::aired says QUEUED (the earned state never shows)" yes \
      's|case mrui::ChanState::aired:      body_text(1, "SENT, waiting");|case mrui::ChanState::aired:      body_text(1, "QUEUED");|'
  ctl "C43 the no-relay outcome reads SENT, no relay again" yes \
      's|"NO RELAY HEARD"|"SENT, no relay"|'
  ctl "C44 DmState::aired_waiting is rendered as QUEUED too" yes \
      's|case mrui::DmState::aired_waiting: body_text(1, "SENT, waiting");|case mrui::DmState::aired_waiting: body_text(1, "QUEUED");|'

  # C45-C56 ★★★ §B197/§B198/§B200 — THE SLEEP SEAM. Each control is a plausible half-fix rather than a deletion, and
  #   the FAILURE MODE OF EVERY ONE IS SILENT: the node keeps meshing, the panel keeps painting, and the only symptom
  #   is either a sleeping node that stops answering the button ([[B197]]) or one that PANICS on a held button
  #   ([[B200]]).
  # ⛔⛔ RETARGETED BY §B200. C46/C47/C48/C52 used to mutate `s_btn_wake_armed = mrui::enable_button_wake();` in
  #   `mr_ui_init()` — a line whose EXISTENCE was the defect. Controls over a defective shape protect the defect.
  # ⛔ C45 IS STILL THE ONE THAT MATTERS MOST. Drop the fail-closed guard and a board whose wake hardware has already
  #   refused light-sleeps anyway, leaving the operator no input at all. It must be RED.
  ctl "C45 the fail-closed guard is dropped (sleeps after a hardware refusal)" yes \
      's|    if (s_sleep_locked_out) return false;|    ;|'
  ctl "C46 the board is never asked (the arm is answered from a constant)" yes \
      's|    switch (mrui::arm_button_wake()) {|    (void)mrui::arm_button_wake(); switch (mrui::WakeArm::armed) {|'
  # ⛔⛔ C47 IS THE [[B200]] BEHAVIOURAL CONTROL, AND IT IS THE SUBTLEST WRONG ANSWER IN THIS FILE: fold `button_down`
  #   into the failure arm and a HELD BUTTON — the most ordinary event there is — disables light-sleep for the whole
  #   boot on a battery-powered safety device, while looking exactly like a working fix.
  ctl "C47 a held button is treated as a hardware failure (latches sleep off for the boot)" yes \
      's|        case mrui::WakeArm::button_down: return MrUiWakeArm::button_down;|        case mrui::WakeArm::button_down: break;|'
  ctl "C48 the arm failure is never said on the console" yes \
      's|    if (latch_sleep_off()) mrcon.println(F("!! OLED button wake unavailable; sleep disabled"));|    ;|'
  # ⓘ C52 is C48's INVERSION and it is what makes the "says nothing when it succeeds" halves mean something: without
  #   it those checks are negative space no mutation could move. Same shape as C9, §B91's report-channel control.
  ctl "C52 the arm report is inverted (silent on the first failure, cries later)" yes \
      's|    if (latch_sleep_off()) mrcon.println(F("!! OLED button wake unavailable; sleep disabled"));|    if (!latch_sleep_off()) mrcon.println(F("!! OLED button wake unavailable; sleep disabled"));|'
  # ★★ C53 THE FLOOD. The arm runs on EVERY idle service pass, so an unlatched print turns one broken board into a
  #   continuous USB-CDC stream — the failure that has already wedged this firmware once. The edge is the fix.
  ctl "C53 the failure is said on every pass, not once (a USB-CDC flood)" yes \
      's|    if (latch_sleep_off()) mrcon.println(F("!! OLED button wake unavailable; sleep disabled"));|    (void)latch_sleep_off(); mrcon.println(F("!! OLED button wake unavailable; sleep disabled"));|'
  # ⛔⛔ C54 IS [[B200]] ITSELF, RE-ADDED. An arm in `mr_ui_init()` is a level-triggered interrupt that outlives every
  #   sleep; holding the button then storms the shared GPIO ISR until the Interrupt watchdog resets the node.
  ctl "C54 an arm is put back into mr_ui_init() (B200, literally)" yes \
      's|    s_model.attach_config(s_cfg);|    (void)mrui::arm_button_wake();\n    s_model.attach_config(s_cfg);|'
  # ★ C55 the disarm's verdict discarded — the tempting "a teardown cannot really fail". It is the one that must not
  #   be missed: a refused disarm means the level is still armed on a RUNNING core.
  ctl "C55 the disarm's answer is discarded (a stuck level is reported as clean)" yes \
      's|    if (mrui::disarm_button_wake()) return true;|    (void)mrui::disarm_button_wake(); return true;|'
  # ★★ C56 an arm hidden in the POLICY hook, which fw_main calls every service pass. It reads like belt-and-braces
  #   ("make sure it is armed before we answer yes") and is a per-pass arm on a pin that may be held — B200 again.
  ctl "C56 the policy hook arms the wake itself (a per-pass arm on a possibly-held pin)" yes \
      's|    if (s_sleep_locked_out) return false;|    if (s_sleep_locked_out \|\| mrui::arm_button_wake() != mrui::WakeArm::armed) return false;|'
  # ★ C57 the boot ANNOUNCES a wake state. Harmless-looking, and it is how the removed §B197 boot line would creep
  #   back: a reader restoring "tell me at boot whether the button can wake us" also restores the expectation that
  #   something was armed there. The boot must say nothing about a wake source, because nothing happens there.
  ctl "C57 the boot announces a wake source again (nothing arms one there)" yes \
      's|^    s_model.attach_config(s_cfg);$|    mrcon.println(F("!! OLED button wake unavailable; sleep disabled"));\n    s_model.attach_config(s_cfg);|'
  # ⓘ C58 is the VACUITY CONTROL for the two failure arms: without it, "a failure answers failed" would also be
  #   satisfied by a mapping that answered `failed` to everything, and P10g would be measuring nothing.
  ctl "C58 a SUCCESSFUL arm is reported as a failure (the vacuity control)" yes \
      's|        case mrui::WakeArm::armed:       return MrUiWakeArm::ok;|        case mrui::WakeArm::armed:       break;|'
  # ★★ C59 the teardown is never performed — the board is not even asked, and `true` is returned. This is [[B200]]
  #   with a clean conscience: every reader sees a disarm, and the pin keeps its level interrupt.
  ctl "C59 the disarm is never performed (the board is not even asked)" yes \
      's|    if (mrui::disarm_button_wake()) return true;|    return true;|'
  # ⓘ C60 is C59's inversion, and it is what makes "says nothing when it succeeds" mean something: a teardown that
  #   reported on its SUCCESS path would put a line on the console after every single sleep.
  # ⚠ It reports rather than LATCHES, deliberately: by the time P12 runs the boot lockout is already spent (P10h
  #   claimed it), so a latch-on-success mutant would be INVISIBLE here — the ordering limitation P12's header
  #   states, arriving as a control that had to be written around it rather than as one that quietly passed.
  ctl "C60 a successful disarm reports a problem anyway (cries wolf)" yes \
      's|    if (mrui::disarm_button_wake()) return true;|    if (mrui::disarm_button_wake()) { mrcon.println(F("!! OLED button wake stuck armed; sleep disabled")); return true; }|'
  # C49/C50 are DIRECTIONAL OPPOSITES on purpose: with only the permissive one, a hook stuck at `false` would satisfy
  # every "refuses" check while the node never slept again; with only the refusing one, the converse.
  ctl "C49 the hook always permits sleep (the whole policy bypassed)" yes \
      's|    return mrui::ui_allows_sleep(s_model, s_input, s_gate);|    return true;|'
  ctl "C50 the hook never permits sleep (the panel-lit answer for ever)" yes \
      's|    return mrui::ui_allows_sleep(s_model, s_input, s_gate);|    return false;|'
  # ⛔ C51 IS THE HALF-FIX THAT WOULD FIX [[B198]] AND LEAVE [[B197]] IN PLACE — inhibit on the frame and the panel,
  #   but not on a gesture still being classified. It is precisely the shape a reader who fixed only the visible
  #   symptom would write, and it re-derives the policy here instead of calling the pure one (U1).
  ctl "C51 the policy is re-derived here without the input term (B197 back)" yes \
      's|    return mrui::ui_allows_sleep(s_model, s_input, s_gate);|    return s_model.state().blanked \&\& !s_gate.frame_open();|'

  ctl "C26 the highlight is NOT suppressed while the refusal stands" yes \
      's|                 (!st.inbox_pick_gone \&\& first + row == st.cursor) ? '"'"'>'"'"' : '"'"' '"'"', tag, e.text, age);|                 (first + row == st.cursor) ? '"'"'>'"'"' : '"'"' '"'"', tag, e.text, age);|'

  # C61-C68 ★★★★ §CHROME-3 — THE STATUS STRIP AND §8.3's INVALIDATION. Each is a plausible edit that leaves every
  #   native case green (the projection is pure and correct in all of them) and every earlier control green too.
  # ⛔⛔ C61 IS THE ONE THIS WHOLE ARCHITECTURE EXISTS TO PREVENT: draw the strip from the LIVE projection instead of
  #   the frozen copy. It looks tidier — the value is right there — and it is correct on any single page. U8g2 replays
  #   the WHOLE scene once per page across eight ticks, so the strip then tears mid-frame, which is visible ONLY to
  #   P13d's per-page comparison.
  ctl "C61 the strip is drawn from the LIVE chrome instead of the frozen copy" yes \
      's|    draw_frame(s_frame_state, s_frame_snap, s_frame_out, s_frame_cfg, s_frame_chrome);|    draw_frame(s_frame_state, s_frame_snap, s_frame_out, s_frame_cfg, live_chrome);|'
  # ⛔ C62 THE INVALIDATION ITSELF. Without it the strip simply goes stale on a lit panel: `FrameGate::step` answers
  #   `idle` for ever on a clean model, and a team route arriving on a beacon or a battery sample landing would never
  #   reach the pixels. It is the POSITIVE half of §8.3.1, and a rule that never invalidates is as wrong as one that
  #   always does.
  ctl "C62 nothing invalidates on a chrome change (the strip goes stale on a lit panel)" yes \
      's|    (void)mrui::ui_chrome_invalidate(s_model, live_chrome, s_frame_chrome);|    (void)live_chrome;|'
  # ⛔⛔ C63 THE REFERENCE UPDATED AT THE **OBSERVATION** INSTEAD OF AT THE FREEZE (§8.3 rule 4). It reads like an
  #   optimisation ("we have seen it, record it") and does two things at once: it consumes the invalidation before any
  #   frame drew it, and — because that same object is what the page loop replays — it rewrites the strip underneath
  #   the pages still to come.
  ctl "C63 the comparison reference is updated where the change is OBSERVED, not at the freeze" yes \
      's|    (void)mrui::ui_chrome_invalidate(s_model, live_chrome, s_frame_chrome);|    if (mrui::ui_chrome_invalidate(s_model, live_chrome, s_frame_chrome)) s_frame_chrome = live_chrome;|'
  # C64/C65 THE GLYPH SELECTION, which is the renderer's own job (§8.1: the firmware owns icon identity and state
  #   selection). Both are pointer-level errors a draw COUNT cannot see, and C65 is the safety-shaped one: a key icon
  #   that says the team content key is HELD when it is absent claims a capability the node does not have.
  ctl "C64 every home state draws the same house (the four-state table bypassed)" yes \
      's|        case mrui::HomeIcon::unknown:   return mrui::icons::kIconHomeUnknown;|        case mrui::HomeIcon::unknown:   return mrui::icons::kIconHomeConfirmed;|'
  ctl "C65 a missing team content key draws the NORMAL key (the slot claims a capability)" yes \
      's|        case mrui::KeyIcon::absent:  return mrui::icons::kIconKeyCrossed;|        case mrui::KeyIcon::absent:  return mrui::icons::kIconKey;|'
  # ⛔ C66 A SLOT MOVED. §3.1 freezes the strip's geometry; the battery token at x = 104 is what makes its last column
  #   land on x = 127 exactly, so moving it either overruns the panel or opens a gap the earlier icons will grow into.
  # ⚠ THE COLUMN IS MOVED FAR ENOUGH THAT THE **NARROW** TOKEN ALSO OVERRUNS, and that is deliberate rather than
  #   dramatic: at 110 the `--` case still fits and P13c's "the strip still fits 128 px" stayed green, i.e. the
  #   control measured only half the property. At 118 both the 4-column `4.1V` (141) and the 2-column `--` (129)
  #   leave the panel, so both the wide and the narrow arm are covered.
  ctl "C66 the battery token's column is moved (the frozen slot redefined)" yes \
      's|    /\* batt \*/ { 91,     104, 127 },|    /* batt */ { 91,     118, 127 },|'
  # ⛔ C67 THE LAYOUT TABLE BYPASSED — every glyph drawn at one x, which is what "the coordinates must not be repeated
  #   at individual draw sites" (§3.1) exists to make impossible. It draws SOMETHING on every page and every count
  #   stays identical, so only a coordinate-level check sees it.
  ctl "C67 every glyph is drawn at the same x (the layout table bypassed)" yes \
      's|    mrui::draw_bitmap(sl.icon_x, kStripIconY, mrui::icons::kIconW, mrui::icons::kIconH, bits);|    mrui::draw_bitmap(0, kStripIconY, mrui::icons::kIconW, mrui::icons::kIconH, bits);|'
  # ⛔ C68 THE BATTERY DRAWN AT THE SHARED 7-PX WIDTH. `stride_of(11)` is 2 and `stride_of(7)` is 1, so U8g2 would
  #   decode the outline's 14 bytes as a 7x14 smear — the exact hazard `firmware_ui_icons.h`'s stride rule names, and
  #   one that a pointer-identity check alone cannot see.
  ctl "C68 the battery is drawn at the shared 7-px width (its 2-byte stride ignored)" yes \
      's|    mrui::draw_bitmap(slot(Strip::batt).icon_x, kStripIconY, mrui::icons::kBatteryW, mrui::icons::kBatteryH,|    mrui::draw_bitmap(slot(Strip::batt).icon_x, kStripIconY, mrui::icons::kIconW, mrui::icons::kIconH,|'
  # ⛔ C69 THE STRIP IS NOT DRAWN AT ALL. The tempting shape is not a deletion but a REORDER — "the body first, then
  #   the header" — and `draw_frame`'s early `return`s under the emergency/compose/detail views would then drop the
  #   strip from exactly the screens §3.1 and §5.3 require it on.
  ctl "C69 the strip is drawn after the body, so body-replacing views lose it" yes \
      's|    draw_status_strip(ch);|    ;|'
  # ⛔ C70 THE STRIP LEAVES ITS OWN BAND. §3.1 gives it y = 0..8 above the y = 9 rule; icons pushed down overlap the
  #   rule and the first body row, which no draw COUNT and no text check can see.
  ctl "C70 the strip's icons are drawn below their y=0..8 band" yes \
      's|constexpr int kStripIconY = 0;|constexpr int kStripIconY = 4;|'
  # ⛔⛔ C71 THE BLANK ARM FALLS THROUGH AND PAINTS ANYWAY — the tempting "keep the image ready for the wake". It is
  #   §8.3.1 behaviour 1's exact forbidden outcome (a dark panel taking bus traffic) and it also spends the power the
  #   whole blank exists to save. ⓘ It leaves W6's `blank -> set_power_save(true)` grep intact, which is why a
  #   behavioural check is needed as well as the structural one.
  ctl "C71 the blanked arm falls through and paints into a dark panel" yes \
      's|            mrui::set_power_save(true);                         // EDGE-triggered|            mrui::set_power_save(true); break;   // EDGE-triggered|'
  # ⛔⛔ C74 A CHROME CHANGE IS TREATED AS USER ATTENTION — and this is the most plausible wrong edit in the whole
  #   slice: "the strip changed, so show it to them". It refreshes `_last_input_ms`, so a node whose home age turns
  #   once a second NEVER blanks and therefore (via `ui_allows_sleep`) never light-sleeps again — a power regression
  #   with no panic and nothing visible on the panel. Design §9 lists "waking the panel for every status-strip change"
  #   as an explicit NON-GOAL, and §8.3.1 rule 1 forbids touching the attention clock.
  ctl "C74 a chrome change is treated as user input (the panel never blanks)" yes \
      's|    (void)mrui::ui_chrome_invalidate(s_model, live_chrome, s_frame_chrome);|    if (mrui::ui_chrome_invalidate(s_model, live_chrome, s_frame_chrome)) s_model.on_gesture(mrui::Gesture::short_press, s);|'
  # ⛔ C72 THE PROJECTION IS BUILT ONCE AND CACHED — the "why rebuild it every tick" optimisation. It is the STALE
  #   PROJECTION §8.3.1 rule 3 names: every later freeze then takes a chrome captured at boot.
  ctl "C72 the live chrome is built once and cached (a stale projection for ever)" yes \
      's|    const mrui::UiChrome live_chrome =|    static const mrui::UiChrome live_chrome =|'
  # ⛔ C73 THE RENDERER FLUSHES A PAGE OF ITS OWN. §11.2: bitmap/frame calls must touch no I2C outside the existing
  #   `next_page()` boundary, and the strip is the first thing in the tree that draws bitmaps at all.
  ctl "C73 the strip opens a frame of its own while composing" yes \
      's|    mrui::draw_hline(0, kBarRuleY, 128);|    mrui::begin_frame();\n    mrui::draw_hline(0, kBarRuleY, 128);|'
  ctl "C75 the strip parks the panel mid-compose (a bus command from a draw)" yes \
      's|    mrui::draw_hline(0, kBarRuleY, 128);|    mrui::set_power_save(true);\n    mrui::draw_hline(0, kBarRuleY, 128);|'
  # ⛔⛔ C76 RETARGETED BY §CHROME-4, AND THE WITHDRAWAL IS RECORDED RATHER THAN THE ENTRY DELETED. It used to read
  #   *"a rail selection frame is drawn (slice 4's scope, one slice early)"* and added a `draw_rect` to prove the
  #   strip drew none — negative space that could only be reverted by ADDING. The rail is now the legitimate caller,
  #   so that mutation would be the CORRECT behaviour. ★ What survives is the half that is still negative space and
  #   is now a SAFETY property: an emergency frame must draw NO rail at all (§5.3), because the body then keeps
  #   x = 0 and the full 128 px that `NOT RELAYED` needs.
  # ⚠ MEASURED, AND THE MEASUREMENT IS WHY THIS CONTROL DROPS **BOTH** CLAUSES: §5.3's suppression is enforced TWICE
  #   over — `rail_visible` is false under an emergency AND `ui_chrome` normalises the slot mask to 0 (so that two
  #   builds which render identically compare equal, §11.1's last rule). Dropping only the `rail_visible` guard
  #   therefore changed NOTHING and this entry was reported UNUSABLE on its first run. ⇒ the control removes the
  #   suppression, not one expression of it.
  ctl "C76 the emergency rail suppression is dropped entirely (§5.3)" yes \
      's|    if (!c.rail_visible) return;|    ;|
       s|        if ((c.slots \& mrui::slot_bit(s)) == 0) continue;      // unavailable: EMPTY, and nothing else moves|        ;|'

  # ============================================================================================ §CHROME-4: C77-C84
  # ★★★★ THE RAIL, THE BADGE AND THE 19-COLUMN BODY. Each entry below is a PLAUSIBLE WRONG IMPLEMENTATION rather than
  #   a deletion, and each leaves the whole native suite green — the projection is pure and correct in all of them,
  #   because every one of these defects lives in the RENDERER, i.e. in the one TU nothing else compiles.
  # ⛔ C77 THE RAIL IS NOT DRAWN AT ALL. The tempting shape is not a deletion but a REORDER — "chrome after the body"
  #   — and `draw_frame`'s early `return`s under the emergency/compose/detail arms would then drop it from exactly
  #   the modal views §5.2 requires it on.
  ctl "C77 the rail is never drawn (the screens lose their only heading)" yes \
      's|    draw_rail(ch);|    ;|'
  # ⛔⛔ C78 EVERY SLOT IS BOXED. §11.2 asks for EXACTLY ONE navigation frame; a rail that framed all five would look
  #   busy rather than wrong, and no count of draw calls could tell the difference. `rail_boxed_slot` answers -2 for
  #   more than one precisely so this cannot pass.
  ctl "C78 every rail slot gets a selection frame (exactly-one becomes all-five)" yes \
      's|        if (c.nav == s) mrui::draw_rect(kRailX, y, kRailW, kRailH);|        mrui::draw_rect(kRailX, y, kRailW, kRailH);|'
  # ⛔⛔ C79 A RENDERER-LOCAL SELECTION CURSOR — the thing §5.1 forbids in as many words ("the selection frame follows
  #   the frozen `UiState::screen`, NEVER a renderer-local cursor"). It is the most plausible wrong edit in the slice
  #   because it looks like an optimisation, and U8g2 replays the scene once per page, so it advances the highlight
  #   EIGHT TIMES inside one frame. Only P14c (all eight page replays) can see it.
  ctl "C79 the selection comes from a renderer-local cursor, not the frozen nav" yes \
      's|        if (c.nav == s) mrui::draw_rect(kRailX, y, kRailW, kRailH);|        static int cursor = 0; if (i == (cursor \&\& 0)) mrui::draw_rect(kRailX, y, kRailW, kRailH); cursor = (cursor + 1) % 5;|'
  # ⛔ C80 THE SECOND LAYOUT §3.2 FORBIDS: pack the available slots consecutively, so an unavailable TEAM/SEND slot
  #   MOVES the icons below it. ⓘ In THIS build all five slots are available, so the mutation is measured through the
  #   arithmetic rather than through a gap — the packed y comes from a running counter and lands one slot high.
  ctl "C80 the rail packs its slots consecutively (a second layout)" yes \
      's|        const int y = rail_slot_y(i);|        static int packed = 0; const int y = rail_slot_y(packed++ % 5) + 1;|'
  # ⛔⛔ C81 THE BODY IS DRAWN AT x = 0 — i.e. the icons land ON TOP OF the text, which is §13's "icons drawn over
  #   21-column content" verbatim. It is the migration's single most likely half-done state.
  ctl "C81 the ordinary body is drawn at x=0, under the rail's icons" yes \
      's|void body_text(int row, const char\* s) { mrui::draw_text(kBodyX, body_y(row), s); }|void body_text(int row, const char* s) { mrui::draw_text(0, body_y(row), s); }|'
  # ⛔⛔ C82 THE EMERGENCY BODY IS MOVED TO kBodyX — the ONE thing §5.3 exists to prevent. `Font::large` is 10 px per
  #   column on a 128-px panel, so a headline drawn at x = 12 has ELEVEN columns and `NOT RELAYED` needs twelve: a
  #   CLIPPED DISTRESS HEADLINE, and nothing else in the tree would notice.
  ctl "C82 the emergency headline is shifted to kBodyX (a clipped distress string)" yes \
      's|    mrui::draw_text(0, kEmgHeadY, head);|    mrui::draw_text(kBodyX, kEmgHeadY, head);|'
  # ⛔ C83 A BODY LINE PUT BACK OVER 19 COLUMNS. `me T255  team ffffffff` is 22 columns and was the reason STATUS's
  #   identity took two rows; restoring the single line is exactly the "it used to fit" edit, and P14f's measured
  #   19-column bound is the only thing that sees it.
  ctl "C83 the STATUS identity goes back onto one 22-column row" yes \
      's|    snprintf(l, sizeof l, "team %08lx", (unsigned long)s.team_id);|    snprintf(l, sizeof l, "me T%u  team %08lx", unsigned(s.my_team_id), (unsigned long)s.team_id);|'
  # ⛔ C84 THE WITHDRAWN STATUS PRESENTATION, RESTORED — and this is §6.1 rule 4's OTHER DIRECTION: *"the badge's
  #   tests must fail against the old STATUS presentation and vice versa, so the two cannot both pass"*. Without it,
  #   a renderer that drew BOTH the badge and the old title marker would satisfy every badge check.
  ctl "C84 the withdrawn CFG marker is put back on the STATUS body (§6.1's other direction)" yes \
      's|    snprintf(l, sizeof l, "me T%u", unsigned(s.my_team_id));|    snprintf(l, sizeof l, "%s", mrui::cfg_marker_text(c.unsaved, c.conflict));|'
  # ⛔ C85 THE SETTINGS INSTRUCTION IS REPLACED BY THE ICON — §6's explicit prohibition ("the icon may replace the
  #   STATUS decoration; it may NEVER replace the instruction"). The badge would still be right and the operator
  #   would have no remedy to read.
  ctl "C85 SETTINGS drops its actionable marker text, leaving only the badge (§6 forbids)" yes \
      's|    if (marker\[0\]) body_text(0, marker);|    ;|'
  # ⓘ C86 IS C85's INVERSION, and it is what makes the "SETTINGS says nothing it cannot act on" half mean something:
  #   without it that check is negative space no mutation could move, and a screen that announced `CFG* UNSAVED` over
  #   a clean draft would pass every other check in this file.
  ctl "C86 the SETTINGS marker row is drawn unconditionally (a clean draft reads UNSAVED)" yes \
      's|    if (marker\[0\]) body_text(0, marker);|    body_text(0, "CFG* UNSAVED");|'
  # ⛔ C87 A RAIL GLYPH DRAWN AT THE BODY'S x — the "centre it in the row" edit. Every count stays identical, the
  #   selection frame stays in the rail, and the icons land on top of the text. Only a coordinate check sees it.
  ctl "C87 the rail's glyphs are drawn at the body's x, over the text" yes \
      's|        mrui::draw_bitmap(kRailX + kRailIconDx, y + kRailIconDy,|        mrui::draw_bitmap(kBodyX, y + kRailIconDy,|'
  # ★★★★ C88/C89 §UI-15 slice 5 — THE OWNER's PARENT-ROW RULING, IN BOTH DIRECTIONS. C88 is the row kept
  #      UNCONDITIONAL (slice 4's shape, i.e. the ruling not applied): the menu then offers PROVISION on a build with
  #      no child, which is the state the ruling forbids. C89 is the OTHER wrong answer and it is why the predicate is
  #      published rather than assumed — a snapshot that reported a child this build does not have would put the row
  #      back through the front door. ⛔ Without C89 the check would be reddened only by an edit to the row builder,
  #      and the PUBLISHING half — which is this file's whole contribution — would be negative space (the §W10b lesson).
  ctl "C88 the PROVISION row is rendered unconditionally (the ruling not applied)" yes \
      's|                                                      mrui::provision_has_child(s.prov_create_team, s.prov_join_static));|                                                      true);|'
  ctl "C89 the child predicates are published as TRUE on a build with no children" yes \
      's|    s.prov_join_static = (MR_N_LAYERS < 2);|    s.prov_join_static = true;|'

  # ============================================================================================= [[B232]]: C90-C92
  # ★★★★ THE SETTINGS SINGLE ENTRY, AT THE RENDERER. The model's half is under the native gate (M97-M104); these are
  #   the three steps NO native case can reach, because nothing but this probe compiles `draw_settings_screen`.
  # ⛔ C90 IS THE RULING NOT APPLIED HERE: the model lands CLOSED and the renderer draws the MENU anyway, so the
  #    operator sees nine rows, presses `short` expecting to walk them, and passes the screen instead. Every native
  #    case stays green — the model is correct in this mutant.
  ctl "C90 the closed view draws the MENU's rows anyway (the ruling not applied at the renderer)" yes \
      's|    if (st.settings == mrui::Settings::closed) {|    if (false) {|'
  # ⛔⛔ C91 IS DESIGN §6's FORBIDDEN ICON-ONLY ERROR, arriving through the closed view: give that view a body of its
  #     own and `CFG* UNSAVED` / `CFG! RELOAD` / `RESTART NEEDED` are readable only after the operator opens a menu
  #     they have no reason to open. The badge would still be right and there would be no remedy to read.
  ctl "C91 the closed view keeps the note/reboot row to itself (§6's icon-only error)" yes \
      's|^        draw_settings_tail(st, c);$|        ;|'
  # ⓘ C92 the entry label RE-SPELLED at the draw site instead of called (U1) — the panel then says something the
  #   native suite's label case cannot see, which is §B115's whole reason for keeping the string in the pure header.
  ctl "C92 the entry row's label is re-spelled at the draw site" yes \
      's|        snprintf(l, sizeof l, ">%s", mrui::kSettingsEnterText);|        snprintf(l, sizeof l, ">SETTINGS");|'

  # ================================================================================= [[B225]]: L1-L9, THE `v3` ARM's
  # ★★★★ THE CONTROLS FOR `draw_provision_screen` ITSELF, AND THEY EXIST ONLY HERE because the screens they mutate are
  #   unreachable on the `l2` arm — a mutation of an unreachable renderer arm is the definition of a control that
  #   measures nothing. Each is the TEMPTING WRONG IMPLEMENTATION rather than a deletion, and each leaves the WHOLE
  #   native suite green: `test_firmware_ui_model.cpp` drives the strings, `test_firmware_ui_prov.cpp` drives the
  #   adapter, and neither compiles this file.
  echo
  echo "-- [[B225]] controls on the child-enabled arm ([env:heltec_v3]) --"
  ARM=v3; ARM_DEFS=LEAF_DEFS
  # ⛔ L1 THE VERDICT ITSELF. A result screen that draws its id and its exit line but not its HEADLINE looks finished
  #    and says nothing about what happened — and `TEAM CREATED` is the one word the operator acts on.
  ctl "L1 the result's headline is dropped (the verdict never shows)" yes \
      's|            body_text(0, mrui::prov_result_head(st.prov_answer));|            ;|'
  # ⛔⛔ L2 THE TWO TOKENS SWAPPED. §3.6.3 draws the FULL id and the SHARED fingerprint one row apart; swapping them is
  #     a copy-paste away, changes no count, and leaves both strings on the panel — only a per-ROW assertion sees it.
  ctl "L2 the id and fingerprint rows are swapped" yes \
      's|                body_text(1, id);|                body_text(1, fp_swap_marker);|
       s|                body_text(2, fp);|                body_text(1, fp);|
       s|                body_text(1, fp_swap_marker);|                body_text(2, id);|'
  # ⛔ L3 THE FINGERPRINT ROW DROPPED — and this is the control the substring trap makes essential: the fingerprint is
  #    the id's own last six characters, so a `strstr` check would still find it inside `0x12A1B2C3` and pass.
  ctl "L3 the fingerprint row is dropped (only the full id is drawn)" yes \
      's|                body_text(2, fp);|                (void)fp;|'
  # ⛔⛔ L4 THE CONDITION RE-SPELLED AT THE DRAW SITE — the exact defect `ui_fmt_prov_replaces`'s block forbids ("the
  #     CONDITION is the formatter's, never this file's"). A teamless node then reads `REPLACES 000000`: a warning
  #     about replacing a team it is not in.
  ctl "L4 the REPLACES warning is composed unconditionally" yes \
      's|            if (mrui::ui_fmt_prov_replaces(note, sizeof note, s.team_id)) body_text(1, note);|            char rfp[mrui::kTeamFpTokenCap]; mrui::ui_fmt_team_fingerprint(rfp, sizeof rfp, s.team_id); snprintf(note, sizeof note, "REPLACES %s", rfp); body_text(1, note);|'
  # ⓘ L5 IS L4's INVERSION, and it is what makes the "absent on a teamless node" half mean something: without it that
  #   check is negative space no mutation could move, and a screen that NEVER warned would pass everything else here.
  ctl "L5 the REPLACES warning is never drawn (§3.6.3 not applied)" yes \
      's|            if (mrui::ui_fmt_prov_replaces(note, sizeof note, s.team_id)) body_text(1, note);|            (void)note;|'
  # ⛔ L6 THE REFUSAL'S DETAIL DROPPED. `CREATE REFUSED` with no reason, `PHY DIFFERS` with no `USE SERIAL`, and
  #    `SAVE FAILED` with no `NOTHING CHANGED` — three screens that state a problem and withhold the remedy.
  ctl "L6 the refusal detail row is dropped (a verdict with no reason)" yes \
      's|            if (detail\[0\]) body_text(1, detail);|            (void)detail;|'
  # ⛔⛔ L7 THE RESULT DRAWN ON THE WRONG ARM — the two `case` labels crossed, which is the §B66 disagreement in its
  #     purest form: the gestures act on the confirmation while the panel draws the result, and vice versa. ⓘ The swap
  #     needs a scratch token because two `s` commands would otherwise undo each other on the same line.
  ctl "L7 the confirm and result arms are crossed (the wrong screen)" yes \
      's|        case mrui::Provision::create_confirm: {|        case mrui::Provision::__swap__: {|
       s|        case mrui::Provision::create_result: {|        case mrui::Provision::create_confirm: {|
       s|        case mrui::Provision::__swap__: {|        case mrui::Provision::create_result: {|'
  # ⛔⛔ L8 THE SEAM. Without the attach the model's `run_create_team` fails closed with `no service` — so this control
  #     is also the PROOF that this arm drives the REAL `UiProvisionAdapter` and not a probe stand-in: nothing else in
  #     the phase would change if the probe were supplying its own `IUiProvision`.
  ctl "L8 the model is never given the provisioning adapter" yes \
      's|    s_model.attach_provision(s_prov_adapter);|    ;|'
  # ⛔ L9 IS C89's MIRROR ON THIS ARM: the CREATE child published as absent on a build that has it. The parent row
  #    survives (JOIN is still published), so the failure is a menu that silently drops the only act it can perform.
  ctl "L9 the CREATE child is published as absent on a build that has it" yes \
      's|    s.prov_create_team = (MR_N_LAYERS < 2) \&\& (MR_FEAT_TEAM != 0);|    s.prov_create_team = false;|'
  # ⛔ L10 IS C88's MIRROR: the owner's HIDE ruling applied where it must NOT be. The parent row vanishes on a build
  #    that HAS children, so §3.6.3 becomes unreachable from the panel — the one direction C88 alone cannot see.
  ctl "L10 the parent row is hidden on a build that HAS a child" yes \
      's|                                                      mrui::provision_has_child(s.prov_create_team, s.prov_join_static));|                                                      false);|'

  # ======================================================================= §UI-15 slice 6: L11-L22, THE JOIN SCREENS
  # ★★★★ THE CONTROLS FOR THE FOUR `join_*` RENDERER ARMS AND FOR `ui_join_note_push`, AND THEY EXIST ONLY HERE for
  #   the reason L1-L10 do: the screens are unreachable on the `l2` arm. Each is the TEMPTING WRONG IMPLEMENTATION
  #   rather than a deletion, and each leaves the WHOLE native suite green — `test_firmware_ui_join.cpp` drives the
  #   rule and the strings, `test_firmware_ui_prov.cpp` the adapter, `test_firmware_ui_model.cpp` the flow, and NONE
  #   of them compiles this file.
  # ⛔ L18 IS THE ONE THAT MATTERS MOST. The correlation rule is pure and mutation-tested; what NOTHING else in the
  #   tree can see is whether THIS file supplies its two facts LIKE FOR LIKE. Feed it the push's own values and every
  #   term it was given collapses into a tautology — a rule that is right and an instrument that is blind.
  ctl "L11 the slot label is drawn from the ROW INDEX instead of the slot number (§B66)" yes \
      's|                else mrui::join_row_label(label, sizeof label, st.join_list.rec.prof\[r.slot1 - 1\], r.slot1);|                else mrui::join_row_label(label, sizeof label, st.join_list.rec.prof[row], uint8_t(row + 1));|'
  ctl "L12 the store-state note is never drawn (an unreadable store looks like an empty list)" yes \
      's|            if (head\[0\]) { body_text(top, head); ++top; }|            (void)head;|'
  ctl "L13 the note is drawn but the list is not moved down (the rows collide with it)" yes \
      's|            uint8_t top = 0;|            uint8_t top = 0; const uint8_t keep_top = 0;|
       s|            const uint8_t rows  = uint8_t(kBodyRows - top);|            top = keep_top; const uint8_t rows = uint8_t(kBodyRows);|'
  ctl "L14 the confirmation shows only the label — design §3.6.3's COMPLETE values are dropped" yes \
      's|            char phy\[mrui::kJoinPhyLineCap\];  mrui::join_fmt_phy(phy, sizeof phy, p);    body_text(1, phy);|            ;|'
  ctl "L15 the confirmation always renders SLOT 1 (the pick is ignored)" yes \
      's|            const mrnv::JoinProfile\& p = st.join_list.rec.prof\[st.join_sel - 1\];|            const mrnv::JoinProfile\& p = st.join_list.rec.prof[0];|'
  # ⛔⛔ L16 IS PLAN §2.3 RULE 1, LITERALLY: the waiting screen claiming membership it has not got.
  ctl "L16 the waiting screen says JOINED before any correlated adopt (plan §2.3 rule 1)" yes \
      's|            body_text(0, mrui::join_wait_head(st.join_still));|            body_text(0, "JOINED");|'
  ctl "L17 the RESULT never shows the adopted node id (the one thing §2.3 rule 2 requires)" yes \
      's|                body_text(1, id);                    // plan §2.3 rule 2: \*"showing the resulting node id"\*|                ;|'
  ctl "L18 ★★ the correlation is fed the PUSH's OWN id, so term 4 becomes a tautology" yes \
      's|    s_model.on_join_push(pu, b.layer0_id, g_node.canonical_node_id());|    s_model.on_join_push(pu, b.layer0_id, pu.dst);|'
  # ⓘ NO BACKTICKS IN THIS LABEL — see the [[B227]] audit above, which is where they cost a gate its meaning. The
  #   label says /mrcfg plainly; the audit forbids any future one from saying it in a way the shell would run.
  ctl "L19 the session guard is dropped (a /mrcfg read on every inbound push)" yes \
      's|    if (!s_model.join_session_active()) return;|    ;|'
  ctl "L20 the push tap is never called, so no adopt can ever complete the screen" yes \
      's|            ui_join_note_push(pu);          // §UI-15 slice 6 — see the function; ⛔ it never displaces the routing above|            ;|'
  # ⛔⛔ L21 IS [[B226]], AND IT IS THE CONTROL THE SLICE SHIPPED WITHOUT. The 60 s word change is a MODEL latch with
  #   its own native cases and its own mutation — but the only thing that puts it ON THE PANEL is this one argument,
  #   and no native suite or corpus compiles this file. Hard-wire `false` and the screen says `JOINING` for ever while
  #   every other gate stays green: the vacuous-coverage class. ⓘ It is DELIBERATELY the surviving-mutant shape rather
  #   than L16's louder `"JOINED"` — P16d would still pass against it, so only P16f's sequence can catch it.
  ctl "L21 ★★ the waiting head is hard-wired to false, so STILL JOINING can never appear" yes \
      's|            body_text(0, mrui::join_wait_head(st.join_still));|            body_text(0, mrui::join_wait_head(false));|'
  # ⛔ L22 IS [[B228]]'s COST, AND ⛔ NOT ITS RULE: dropping the prefilter changes no verdict whatsoever (the rule's
  #   own kind gate still rejects the push one call later), so the ONLY thing that can catch it is the /mrcfg read
  #   count P16e now takes. A control that reddens a CORRECTNESS check would be measuring the wrong property.
  ctl "L22 the kind prefilter is dropped (every push pays a /mrcfg read again)" yes \
      's|    if (pu.kind != MESHROUTE_NS::PushKind::join_adopted) return;.*|    ;|'
  ARM=l2; ARM_DEFS=DEFS
fi

# ---- ★★ §T3 (design P6) — THE OLD STRING MUST BE GONE FROM EVERY RENDERING SOURCE ------------------------------
# ⛔ SCOPE IS `src/` + `tools/`, DELIBERATELY, AND NOT TREE-WIDE. The bench guide and the bug register legitimately
#   QUOTE `SENT, no relay` inside withdrawn-wording blocks (a withdrawal is kept visible, never deleted), so a
#   tree-wide grep would force those histories to be erased to make a gate green — the wrong trade. What must be true
#   is that nothing a panel can PRINT still carries it.
echo
echo "== §T3 P6: the retired string is absent from every rendering source =="
# ⚠ COMMENTS ARE STRIPPED FIRST (§B77's lesson, borrowed from `probe_board_ui`'s `code_flat`): the rename is
#   RECORDED in a comment beside the arm it changed, and a bare grep matches the note that says the string is gone.
#   What must be absent is a string a panel can PRINT, i.e. code.
: > "$OUT/p6.txt"
while IFS= read -r f; do
  sed 's://.*::' "$f" | grep -n -F 'SENT, no relay' | sed "s|^|$f:|" >> "$OUT/p6.txt"
done < <(find "$ROOT/src" "$ROOT/tools" \( -name '*.cpp' -o -name '*.h' \) )
if [ -s "$OUT/p6.txt" ]; then
  echo "  FAIL 'SENT, no relay' still present in a rendering source:"; sed 's/^/    /' "$OUT/p6.txt" | head -10; rc=1
else
  echo "  ok   'SENT, no relay' appears in no src/ or tools/ source"
fi
# ⚠ AND THE VACUITY GUARD FOR IT (the grep must be able to FIND things): the string that REPLACED it must be present.
if grep -rq --include=*.cpp -F 'NO RELAY HEARD' "$ROOT/src"; then
  echo "  ok   ...and its replacement IS present, so the grep is not matching nothing"
else
  echo "  FAIL the replacement string is absent too — the check above proved nothing"; rc=1
fi

# ---- the tree must be exactly as we found it -------------------------------------------------------------------
MD5_AFTER=$(cat "$FW_UI" "$HERE/probe_main.cpp" "$FAKES/Arduino.h" "$ROOT/src/fw_context_pure.h" \
                "$ROOT/src/firmware_ui_prov.h" "$ROOT/lib/hal/device_hal.h" | md5sum | cut -d' ' -f1)
if [ "$MD5_BEFORE" != "$MD5_AFTER" ]; then
  echo "  FAIL the probe MODIFIED a real source file ($MD5_BEFORE -> $MD5_AFTER)"; rc=1
else
  echo
  echo "sources unchanged: md5 $MD5_AFTER"
fi

# ---- ★★ THE COVERAGE ROLL-UP: which checks can a control actually break? ----------------------------------------
# A green probe with green controls still says nothing about the checks NO control touches. This prints the ratio and
# NAMES the exceptions, so an un-reddened check has to be justified in the source rather than assumed covered.
if [ -s "$OUT/all_checks-l2.txt" ] && [ "${1:-}" != "--no-neg" ]; then
  # ⛔ THE RATIO IS OVER THE **UNION** OF THE TWO ARMS, and the alternative was measured and rejected: a PER-ARM ratio
  #   reported every shared check as uncovered on `v3` (240 of them), because the control that breaks it runs against
  #   `l2`. The question this roll-up exists to answer is *"is there a check NO control anywhere can break?"* — so the
  #   denominator is every label either arm runs and the numerator is every label either arm's controls reddened.
  cat "$OUT/all_checks-l2.txt" "$OUT/all_checks-v3.txt" | sort -u > "$OUT/all_checks.txt"
  cat "$OUT/reddened-l2.txt"   "$OUT/reddened-v3.txt"   | sort -u > "$OUT/reddened_u.txt"
  n_all=$(grep -c . "$OUT/all_checks.txt"); n_red=$(grep -c . "$OUT/reddened_u.txt")
  echo
  echo "checks per arm: l2 $(grep -c . "$OUT/all_checks-l2.txt") · v3 $(grep -c . "$OUT/all_checks-v3.txt")"
  echo "coverage: $n_red of $n_all checks (both arms, unioned) are reddened by at least one control"
  comm -23 "$OUT/all_checks.txt" "$OUT/reddened_u.txt" | sed 's/^/  (no control reddens) /'
elif [ "${1:-}" != "--no-neg" ]; then
  echo "  FAIL the check roll-up produced 0 labels — PROBE_LIST is not wired, so the ratio would be vacuous"; rc=1
fi

echo "controls: $n_ctl verified / $n_bad unusable"
[ "$n_bad" -eq 0 ] || rc=1
if [ "$rc" -eq 0 ]; then echo "PASS"; else echo "FAIL"; fi
exit $rc
