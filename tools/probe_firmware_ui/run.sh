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
#     WRONG FIX, not merely a deletion — and must make the probe RED. FOUR ways a control can be worthless, all four
#     checked and all four have fired in this arc:
#       1. the sed matched nothing        -> the copy is byte-identical  -> reported VACUOUS, counted as a failure;
#       2. the mutant does not compile    -> the probe never ran         -> reported, counted as a failure
#          (C0 is the ONE exception: it is the BUILD control and is REQUIRED to fail compilation);
#       3. the probe still passes         -> the check does not measure the property -> counted as a failure;
#       4. ★★ [[B237]], ADDED 2026-08-21 — the mutant **DID NOT REPORT, IT DIED**: a signal, an abort, or any exit
#          the probe cannot produce, or a non-zero exit with ZERO `  FAIL ` lines. The probe answers 0 or 1 and
#          nothing else, so anything else means it stopped mid-run and the checks after that point never executed.
#          ⛔ Such a control is UNUSABLE, ⛔ never "verified" — see `classify_control`, which owns the whole rule and
#          carries its own crashing repro. **MEASURED (QG): C81's mutant segfaulted with zero failed checks and the
#          old `if binary; then … else RED` scored it as a successful reddening.**
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
# ★★★★ [[B229]] — THE ATTRIBUTION IS **LENGTH-PROOF**, AND THAT CLOSES THE MECHANISM RATHER THAN THE SYMPTOM
# ---------------------------------------------------------------------------------------------------------------
# WHAT WAS WRONG. A reddening used to be attributed by re-reading the FAIL line with
# `s/^  FAIL \(.\{1,64\}\)  .*$/\1/` — a parse that depends on the `%-64s` PADDING to find the label's end. A label
# LONGER than 64 characters overflows the field, the expression matches nothing (or, worse, an earlier double space
# INSIDE the label), and the roll-up then reports a check that a control DOES redden as *"(no control reddens)"*.
# ⛔ It is an UNDER-count, never a false PASS — the instrument lying in the quiet direction.
# ⚠ MEASURED, and it was live in this tree: `C84` reddens *"P7b ...and the STATUS body no longer carries the
#   withdrawn marker TEXT"* (70 characters, and §UI-17 S3 re-pointed C84 onto exactly that check), and the roll-up
#   listed that check as un-reddened.
# ★★ THE FIX IS THE PARSE, NOT THE LABELS. Shortening the four long labels would have closed THIS occurrence and left
#    the mechanism armed for the next one (three labels had already been shortened once, [[B226]]/[[B228]]). The
#    label list the roll-up already builds — every label the arm's probe announced under `PROBE_LIST=1` — is the
#    authority for where a label ENDS, so attribution needs no delimiter at all: match the FAIL line against the
#    KNOWN labels and take the LONGEST that prefixes it. ⓘ Longest-wins is what keeps a label that is a prefix of
#    another from stealing its attribution.
# ⇒ `CHK`'s own 64-character note in `probe_main.cpp` is corrected in place with this (V1: fix the comments you touch).
attribute() {   # attribute <labels-file> <probe-output> -> the labels whose checks FAILED, one per line
  awk 'NR==FNR { lab[FNR] = $0; n = FNR; next }
       /^  FAIL /{ s = substr($0, 8); best = "";
                   for (i = 1; i <= n; i++) { l = lab[i]
                       if (index(s, l) == 1 && length(l) > length(best)) best = l }
                   if (best != "") print best }' "$1" "$2"
}
echo
echo "== [[B229]] the coverage roll-up attributes a label of ANY length =="
# ⚠ THE GUARD IS SELF-CONTAINED, so it keeps measuring the property even if every real label were shortened one day:
#   an ARTIFICIAL 90-character label is added to a COPY of the list and a synthetic FAIL line is built for it in the
#   exact `%-64s` shape `CHK` prints. ⛔ Not a hand-written expectation of the parse — the same `attribute` the
#   controls use is what is run.
b229_long="P0 an artificial label deliberately past the sixty-four character field, to prove the parse"
{ cat "$OUT/all_checks-l2.txt"; printf '%s\n' "$b229_long"; } > "$OUT/b229_labels.txt"
printf '  FAIL %-64s  some_expression\n' "$b229_long" > "$OUT/b229.out"
b229_seen=$(attribute "$OUT/b229_labels.txt" "$OUT/b229.out")
if [ "$b229_seen" = "$b229_long" ]; then
  echo "  ok   a ${#b229_long}-character label is attributed to its control"
else
  echo "  FAIL a long label is still not attributed: [$b229_seen]"; rc=1
fi
# ⚠ AND THE VACUITY GUARD FOR IT (§T3 P6's rule): a "fix" that changed nothing would pass the check above just as
#   happily. The WITHDRAWN parse is run over the SAME synthetic line and is REQUIRED to miss it — which is what says
#   the defect was real and that this run is measuring its absence.
b229_old=$(sed -n 's/^  FAIL \(.\{1,64\}\)  .*$/\1/p' "$OUT/b229.out")
if [ "$b229_old" != "$b229_long" ]; then
  echo "  ok   ...and the WITHDRAWN 64-column parse does miss it, so the defect was real"
else
  echo "  FAIL the old parse attributed it too — this run proved nothing"; rc=1
fi

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

# ---------------------------------------------------------------------------------------------------------------
# ★★★★ [[B237]] — WHAT COUNTS AS "THE PROBE WENT RED", AS ONE FUNCTION
# ---------------------------------------------------------------------------------------------------------------
# ⛔ THE DEFECT THIS REPLACES: `if "$OUT/mutant.bin"; then …not red… else …RED… fi` — i.e. ANY non-zero exit was
#   scored as a successful control. **MEASURED (QG, 2026-08-21):** control C81's mutant SEGFAULTED (signal 139) with
#   ZERO failed checks and the runner reported it VERIFIED. A crash is not a measurement: the probe stopped
#   somewhere in the middle, so nothing is known about the checks that never ran — and the roll-up then attributes
#   nothing while the count says the property is covered. That is the quiet direction again.
# ★ THE PROBE'S OWN CONTRACT MAKES THE RULE EXACT: `probe_main.cpp`'s `main` ends `return g_fail == 0 ? 0 : 1;` —
#   0 = every check passed, 1 = at least one check FAILED, and it has no third answer. Anything else (a signal's
#   128+n, an abort, a library exit) is the binary dying rather than reporting.
# ⇒ RED requires BOTH: a NORMAL assertion-failure exit (exactly 1) **and** at least one `  FAIL ` line to name what
#   was reddened. Everything else is UNUSABLE and is counted against the gate.
classify_control() {   # classify_control <exit-code> <fail-line-count> -> red | passes | abnormal | silent
  local rc=$1 fails=$2
  if   [ "$rc" -eq 0 ];    then printf 'passes'    # the mutant satisfied every check — the property is not measured
  elif [ "$rc" -ne 1 ];    then printf 'abnormal'  # a signal / abort / any exit the probe cannot produce = a crash
  elif [ "$fails" -eq 0 ]; then printf 'silent'    # "failed" without naming one failure — nothing to attribute
  else                          printf 'red'
  fi
}

# ⚠ THE CONTROL-OF-THE-CONTROL, and it is END-TO-END rather than a table of arguments ([[B217]]'s precedent: the fix
#   carries its own repro). A binary that REALLY dies of SIGSEGV is built and run through the SAME `$?` path `ctl`
#   uses, and its verdict must come back `abnormal`. ⛔ Without this the new rule is itself unmeasured — which is
#   the exact shape B237 is about.
echo
echo "== [[B237]] a CRASHING control is UNUSABLE, never verified =="
printf 'int main() { volatile int* p = 0; return *p; }\n' > "$OUT/crasher.cpp"
if "$CXX" -O0 "$OUT/crasher.cpp" -o "$OUT/crasher" 2>/dev/null; then
  b237_rc=0
  bash -c '"$1"; exit $?' _ "$OUT/crasher" > "$OUT/crash.out" 2>&1 || b237_rc=$?
  b237_fails=$(grep -c '^  FAIL ' "$OUT/crash.out")
  b237_verdict=$(classify_control "$b237_rc" "$b237_fails")
  if [ "$b237_rc" -ge 128 ] && [ "$b237_verdict" = abnormal ]; then
    echo "  ok   a real SIGSEGV (exit $b237_rc, $b237_fails failures) classifies as UNUSABLE"
  else
    echo "  FAIL a crashing control classified as '$b237_verdict' (exit $b237_rc) — the rule does not hold"; rc=1
  fi
  # ⚠ AND THE OTHER THREE ARMS, so the rule is not merely "reject everything": the shape that MUST still be RED, the
  #   one that must read `passes`, and the non-zero-but-silent one the old rule could not tell from a real reddening.
  if [ "$(classify_control 1 3)" = red ] && [ "$(classify_control 0 0)" = passes ] \
     && [ "$(classify_control 1 0)" = silent ]; then
    echo "  ok   ...and a genuine 1/3 stays RED, 0/0 is passes, 1/0 is silent — it still discriminates"
  else
    echo "  FAIL the classifier no longer separates a real reddening from a crash"; rc=1
  fi
  # ⓘ The shell's own `Segmentation fault` notice is captured, ⛔ not printed: a gate whose PASS output contains a
  #   crash message trains the reader to ignore crash messages.
  if grep -qiE 'segmentation|core dumped' "$OUT/crash.out"; then
    echo "  ok   ...and the shell's crash notice was CAPTURED, not printed to this gate"
  else
    echo "  ok   ...and the crash produced no console notice at all"
  fi
else
  echo "  FAIL the crash repro did not build — the new rule is unmeasured"; rc=1
fi

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
  # ⛔⛔ [[B237]] — THE VERDICT IS `classify_control`'s, ⛔ NEVER A BARE "did it exit non-zero". A CRASHING mutant
  #     exits non-zero having measured NOTHING, and this line used to score that as a successful reddening.
  # ⓘ THE RUN GOES THROUGH AN INNER `bash -c`, and the trailing `exit $?` is load-bearing rather than decoration:
  #   bash EXECs a LONE final command, which would leave OUR shell to print the `Segmentation fault` notice on its
  #   own stderr — i.e. on the gate's console. With the extra statement the inner shell survives to report the death
  #   into the capture file instead, where the `abnormal` arm above tails it. ⛔ A gate whose PASS output carries
  #   crash noise teaches its reader to skip crash noise.
  local rc_m=0
  bash -c '"$1"; exit $?' _ "$OUT/mutant.bin" >"$OUT/mutant.out" 2>&1 || rc_m=$?
  local fails; fails=$(grep -c '^  FAIL ' "$OUT/mutant.out")
  local verdict; verdict=$(classify_control "$rc_m" "$fails")
  if [ "$verdict" = passes ]; then
    n_bad=$((n_bad+1))
    printf '  FAIL %s — the probe still PASSES against the mutant (the check measures nothing)\n' "$label"
  elif [ "$verdict" = abnormal ]; then
    n_bad=$((n_bad+1))
    printf '  FAIL %s — the mutant DIED (exit %s, %s reported failure(s)); a crash measures nothing\n' \
           "$label" "$rc_m" "$fails"
    sed 's/^/        /' "$OUT/mutant.out" | tail -4
  elif [ "$verdict" = silent ]; then
    n_bad=$((n_bad+1))
    printf '  FAIL %s — exit %s with ZERO reported failures; nothing names what it reddened\n' "$label" "$rc_m"
  else
    n_ctl=$((n_ctl+1))
    printf '  ok   %s -> RED (%s check(s) failed)\n' "$label" "$fails"
    # record WHICH checks this control reddened, for the roll-up. ⛔ NOT by re-parsing the `%-64s` field ([[B229]]):
    # `attribute` matches the FAIL line against the arm's KNOWN label list, so a label of ANY length is attributed.
    # ⓘ INTO THE CURRENT ARM's file: a control mutates ONE build, so its evidence belongs to that arm's ratio.
    attribute "$OUT/all_checks-$ARM.txt" "$OUT/mutant.out" >> "$OUT/reddened-$ARM.txt"
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
  # ⛔⛔ C35 RETARGETED BY §UI-17 S3, AND THE RETARGETING IS RECORDED RATHER THAN THE CONTROL QUIETLY DELETED. It used
  #   to mutate `if (c.reboot) { body_text(4, mrui::kCfgRestartText); return; }` — the priority test that STOOD IN
  #   THIS FILE. S3 moves that decision into the pure `mrui::ui_status_location` (spec §2.2 note g), where a battery
  #   can attack it, so a sed for the old line would now match NOTHING and be reported VACUOUS. ★ THE FACT IT
  #   GUARDED DID NOT GO AWAY — the renderer still has to HAND the reboot fact over, and that seam is what this
  #   control now attacks: pass `false` and the panel never says `RESTART NEEDED` again, which is exactly what the
  #   old control described one presentation earlier. ⓘ The PRIORITY itself (restart over coordinates) is covered by
  #   `--target=uistatus`, which is the split S3 exists to create.
  ctl "C35 RESTART NEEDED never reaches STATUS (the reboot fact is dropped)" yes \
      's|    mrui::ui_status_location(l, sizeof l, c.reboot, s);|    mrui::ui_status_location(l, sizeof l, false, s);|'
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

  # ⚠ C26's ANCHOR MOVED 2026-08-20 (§UI-17 S1 gave both list renderers ONE marker predicate); the property it
  #   mutates — §B64's suppression while a refusal stands — is unchanged, and the mutation still removes exactly that
  #   term and nothing else.
  ctl "C26 the highlight is NOT suppressed while the refusal stands" yes \
      's|        const bool here = entered \&\& !st.inbox_pick_gone \&\& idx == st.cursor;|        const bool here = entered \&\& idx == st.cursor;|'

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
  # ⛔⛔ C83 RETARGETED BY §UI-17 S3, AND THE RETARGETING IS RECORDED RATHER THAN THE CONTROL DELETED (the C14-C16 /
  #   C33 / C38 precedent). Its sed named `snprintf(l, sizeof l, "team %08lx", …)` — a string S3 MOVED into the pure
  #   `firmware_ui_status.h`, so it would now match NOTHING and be reported VACUOUS (it was, on the first S3 run).
  #   ★ THE FACT IT GUARDED IS UNCHANGED and got HARDER: the identity rows are now at `x = 40` with **14** columns,
  #   so folding them back onto one line is 21 columns in a 14-column slot. Same edit, same "it used to fit"
  #   temptation, measured by P14f's narrowed budget as well as its 19-column one.
  ctl "C83 the STATUS identity goes back onto one 21-column row" yes \
      's|    mrui::ui_status_team(l, sizeof l, s);         status_text(0, l);|    { char t\[kLineCap\], m\[kLineCap\]; mrui::ui_status_team(t, sizeof t, s); mrui::ui_status_me(m, sizeof m, s); snprintf(l, sizeof l, "%s %s", t, m); } status_text(0, l);|'
  # ⛔ C84 THE WITHDRAWN STATUS PRESENTATION, RESTORED — and this is §6.1 rule 4's OTHER DIRECTION: *"the badge's
  #   tests must fail against the old STATUS presentation and vice versa, so the two cannot both pass"*. Without it,
  #   a renderer that drew BOTH the badge and the old title marker would satisfy every badge check.
  # ⛔⛔ C84 RETARGETED BY §UI-17 S3 FOR THE SAME REASON, AND ITS FACT IS NOW ALSO A RULING (§9 R-3: *no
  #   configuration text returns to STATUS*). Its sed named the deleted `snprintf(l, sizeof l, "me T%u", …)`; the
  #   row it mutated is `status_text(1, …)` now, so the control follows it there. ⛔ The direction is §6.1 rule 4's
  #   OTHER one — without it, a renderer that drew BOTH the badge and the withdrawn body text satisfies every badge
  #   check — and P7b's *"the STATUS body no longer carries the withdrawn marker TEXT"* is what it reddens.
  ctl "C84 the withdrawn CFG marker is put back on the STATUS body (§6.1's other direction)" yes \
      's|    mrui::ui_status_me(l, sizeof l, s);           status_text(1, l);|    snprintf(l, sizeof l, "%s", mrui::cfg_marker_text(c.unsaved, c.conflict));           status_text(1, l);|'
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
  # ⚠ RE-ANCHORED 2026-08-23 (§UI-16 N2): the predicate gained a third child and was hoisted to its own line, so
  #   this control now mutates the LOCAL rather than the argument. Its MEANING is unchanged — the parent row is
  #   offered whatever the children say.
  # ⚠ RE-ANCHORED AGAIN 2026-08-25 (§UI-16 K6): the predicate gained a FIFTH child (`SAVED KEYS`). The call is kept
  #   on ONE line for exactly this reason — `sed` cannot match across a newline, and a wrapped call would leave this
  #   control anchored on a fragment and VACUOUS. Meaning unchanged.
  ctl "C88 the PROVISION row is rendered unconditionally (the ruling not applied)" yes \
      's|        mrui::provision_has_child(s.prov_create_team, s.prov_join_static, s.prov_join_team, s.prov_invite, s.prov_saved_keys);|        true;|'
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

  # ============================================================================== §UI-17 S1: C93-C95, THE TWO LISTS
  # ★★★★ TEAM/INBOX PASSIVE ↔ INTERACTIVE, AT THE RENDERER. The model's half is under the native gate (`ui17-` cases
  #   and M106-M120); these are the three steps NO native case can reach, because nothing but this probe compiles
  #   `draw_team_screen` / `draw_inbox_screen`. ⓘ ONE sed hits BOTH screens: the predicate line is deliberately
  #   identical in the two functions (U1), so a control cannot cover one list and quietly miss the other.
  # ⚠ C93/C94 ARE DIRECTIONAL OPPOSITES on purpose, the C49/C50 shape: with only C93 a renderer stuck at "never
  #   entered" would satisfy every "no marker while passive" check while the operator could never select anything;
  #   with only C94, the converse.
  ctl "C93 the list is drawn as ENTERED whatever the model says" yes \
      's|    const bool entered = mrui::screen_is_entered(st.screen, st.settings, st.list_view);|    const bool entered = true;|'
  ctl "C94 an entered list is drawn as PASSIVE (no marker, no exit)" yes \
      's|    const bool entered = mrui::screen_is_entered(st.screen, st.settings, st.list_view);|    const bool entered = false;|'
  # ⛔ C95 IS THE CONTAINMENT'S OWN CONTROL: the row exists in the model and the renderer never draws it, so the
  #    operator is inside a list whose only exit is invisible — and `short` cannot leave it either (that is the whole
  #    point of the contained walk). Two seds, because the two screens draw it on different rows (the INBOX list is
  #    offset by its header).
  ctl "C95 the interactive list's BACK row is never drawn" yes \
      's|== mrui::ListRow::back) { body_back_row(row, here); continue; }|== mrui::ListRow::back) { continue; }|
       s|== mrui::ListRow::back) { body_back_row(row + 1, here); continue; }|== mrui::ListRow::back) { continue; }|'

  # ================================================================ §UI-17 S3: C96-C104, THE STATUS BODY AT THE SEAM
  # ★★★★ THE GEOMETRY **AND THE HANDOFF**, AT THE RENDERER. Every STRING and every substitution is pure and is
  #   attacked by `--target=uistatus`; what NOTHING there can see is WHERE this file puts them, WHETHER IT PLACES
  #   THEM AT ALL, and whether the reserved slot is drawn — `src/firmware_ui.cpp` is compiled by neither the native
  #   suite nor the simulator (§B115). C96-C99 are the geometry (spec §4's S3 Probe bullet, plus the two
  #   inversions); C100-C104 are the PRODUCTION HANDOFF and the FRAME FREEZE, which P17 measures.
  # ⓘ COUNT CORRECTED IN PLACE 2026-08-21: this header said "C96-C98 / three controls" while the block already held
  #   four. The figure is now the range and it is re-read from the block, not remembered.
  # ⛔ C96 THE MARK NEVER DRAWN. ⓘ RE-POINTED BY §UI-17 S6 at the `draw_bitmap` that replaced S3's placeholder rect
  #    — the control is the SAME question (does the reserved slot get drawn at all?) asked of the line that now
  #    answers it. P14a's screen-dependent census is what sees it, through the asset's pointer identity.
  ctl "C96 the reserved 24x24 mark is never drawn (the slot vanishes)" yes \
      's|    mrui::draw_bitmap(kStatusMarkX, kStatusMarkY, kStatusMarkW, kStatusMarkH, mrui::icons::kMarkMeshRoute);|    ;|'
  # ⛔⛔ C97 IS THE ONE SPEC §2.1 NAMES: rows 0-2 drawn at the BODY origin, i.e. straight through the reserved slot.
  #     It is the tempting edit ("one body_text for all five rows, the way every other screen does it"), it leaves
  #     every native case and every uistatus mutation green, and on glass it puts three lines of text ON TOP of the
  #     mark. P14f's per-screen origin SET plus its row-level split is what reddens it.
  ctl "C97 rows 0-2 are drawn at the body origin, through the reserved mark" yes \
      's|void status_text(int row, const char\* s) { if (s\[0\]) mrui::draw_text(kStatusTextX, body_y(row), s); }|void status_text(int row, const char* s) { if (s[0]) mrui::draw_text(kBodyX, body_y(row), s); }|'
  # ⓘ C98 IS C97's INVERSION and it is what makes the 14-column budget mean something: move rows 3-4 up to the
  #   NARROWED origin and the two widest lines in this body (18 and 16 columns) are drawn in a 14-column slot, off
  #   the right edge. Without it, "no x=40 row exceeds 14 columns" is negative space no mutation could move.
  ctl "C98 row 3 is drawn at the NARROWED origin (18 columns in 14)" yes \
      's|    mrui::ui_status_unread_home(l, sizeof l, s);  body_text(3, l);|    mrui::ui_status_unread_home(l, sizeof l, s);  status_text(3, l);|'
  # ⓘ C99 IS C96's INVERSION and it is what makes *"an ordinary screen's body draws no rect of its own"* mean
  #   something: without it that check is negative space no mutation could move. The edit is the tempting one —
  #   "the mark is branding, put it in the chrome beside the strip" — and it draws the 24x24 mark straight through the
  #   TEAM roster and the INBOX list on every screen but the one it belongs to.
  ctl "C99 the mark is drawn in the chrome, so EVERY screen shows it" yes \
      's|    mrui::draw_hline(0, kBarRuleY, 128);|    mrui::draw_hline(0, kBarRuleY, 128);\n    mrui::draw_bitmap(kStatusMarkX, kStatusMarkY, kStatusMarkW, kStatusMarkH, mrui::icons::kMarkMeshRoute);|'
  # ⛔⛔ C100/C101 ARE THE HANDOFF ITSELF, and they are the controls this slice SHIPPED WITHOUT until QG found the
  #   hole. The five rows are pure, natively pinned and mutation-covered — but every one of those instruments calls
  #   `mrui::ui_status_*` DIRECTLY. Drop the call from this file, or point it at the wrong baseline, and the whole
  #   pure gate stays green while the panel loses (or duplicates) a row. P17a's exact-bytes-at-exact-coordinate
  #   checks are the only thing in the tree that can see it — [[B226]]'s discipline at the production seam.
  ctl "C100 STATUS row 2 is never placed (the pure row is composed and dropped)" yes \
      's|    mrui::ui_status_known(l, sizeof l, s);        status_text(2, l);|    mrui::ui_status_known(l, sizeof l, s);|'
  ctl "C101 STATUS row 2 is placed on row 1's baseline (a misroute)" yes \
      's|    mrui::ui_status_known(l, sizeof l, s);        status_text(2, l);|    mrui::ui_status_known(l, sizeof l, s);        status_text(1, l);|'
  # ⛔⛔⛔ C102 IS THE DEFECT S3's FIRST CUT ACTUALLY SHIPPED, re-added deliberately so it can never come back
  #     quietly: read `g_node.config()` LIVE in the renderer. `draw_frame` runs ONCE PER OLED PAGE, so a `cfg set
  #     lat` between two of the eight replays draws HALF THE COORDINATE ROW from each fix. ⛔ Every native case,
  #     every uistatus mutation and every geometry check stays GREEN against it — P17b is the only witness.
  ctl "C102 row 4 reads the LIVE config again (the row can TEAR mid-frame)" yes \
      's|    mrui::ui_status_location(l, sizeof l, c.reboot, s);|    mrui::UiSnapshot live = s; const MESHROUTE_NS::NodeConfig\& lc = g_node.config(); live.own_lat_e7 = lc.lat_e7; live.own_lon_e7 = lc.lon_e7; live.own_fix = mrui::ui_status_have_fix(lc.lat_e7, lc.lon_e7); mrui::ui_status_location(l, sizeof l, c.reboot, live);|'
  # ⛔ C103/C104 THE PUBLISH SITE's OWN TWO WRONG ANSWERS. `own_fix` is the ONE predicate's answer written at the one
  #   place that sees `NodeConfig`; hardcode it and a node with no position claims `0.000,0.000` (the plausible
  #   substitution this screen exists to refuse), narrow it to one coordinate and a node on the equator loses its.
  ctl "C103 own_fix is published as a constant (a fix that was never configured)" yes \
      's|    s.own_fix              = mrui::ui_status_have_fix(own_cfg.lat_e7, own_cfg.lon_e7);|    s.own_fix              = true;|'
  ctl "C104 own_fix is published from ONE coordinate (the OR narrowed to lat)" yes \
      's|    s.own_fix              = mrui::ui_status_have_fix(own_cfg.lat_e7, own_cfg.lon_e7);|    s.own_fix              = (own_cfg.lat_e7 != 0);|'

  # ================================================================= §UI-17 S4: C105-C112, THE TEAM ROW AT THE SEAM
  # ★★★★ THE HANDOFF, AND IT IS THE ONE THING `--target=uiteam` AND `test_firmware_ui_team.cpp` STRUCTURALLY CANNOT
  #   SEE: both call `mrui::ui_team_row` directly. Point it at the wrong snapshot row, hand it the wrong marker, draw
  #   the result at the wrong origin, or stop calling it at all — and every native case stays green, all SIXTEEN
  #   mutations stay RED, and the panel shows the wrong people. (ⓘ count corrected 2026-08-21, QG: said "twelve".) `src/firmware_ui.cpp` is compiled by neither the
  #   native suite nor the simulator (§B115), so this file is the only venue that can redden any of it.
  # ⛔ C105 IS THE REVERT TEMPTATION, spelled out rather than deleted: the pre-S4 row, composed inline — a nine-column
  #    label, a four-column age and the HOP COUNT that spec §3.2 ruled off the row. It is what a reader who "restored"
  #    the old line would produce, and it is 19 columns too, so only the per-row byte assertions can catch it.
  # ⓘ THE MARKER IS WRITTEN AS 62/32 — the codes of `>` and a space — purely so this control's sed carries no nested
  #   shell quotes; the [[B227]] audit above is about labels, and a script that needed escaping is a script that gets
  #   one character wrong. `%c` takes an int either way.
  ctl "C105 the pre-S4 row (9-column label + hops) is composed inline again" yes \
      's|        mrui::ui_team_row(l, sizeof l, here, s.team\[idx\], own);|        { char a_[kAgeCap]; fmt_age(a_, sizeof a_, s.team[idx].last_heard_s); snprintf(l, sizeof l, "%c%-9.9s %4.4s %uh", here ? 62 : 32, s.team[idx].label, a_, unsigned(s.team[idx].hops)); }|'
  # ⛔⛔ C106 IS THE MISROUTE, and it is the reason every row is asserted at its OWN coordinate: a renderer that drew
  #     the FIRST teammate on every row keeps the format, the width and the origin — and names the wrong person.
  ctl "C106 every row is drawn from snapshot row 0 (the wrong teammate)" yes \
      's|        mrui::ui_team_row(l, sizeof l, here, s.team\[idx\], own);|        mrui::ui_team_row(l, sizeof l, here, s.team[0], own);|'
  # ⛔ C107 THE MARKER DROPPED AT THE SEAM. The pure unit renders whatever marker it is handed, so "the selected row
  #    is marked" is THIS file's claim and nothing else's — §B64's suppression is one argument away from silent.
  ctl "C107 the row marker is hard-wired off (no pick is ever shown)" yes \
      's|        mrui::ui_team_row(l, sizeof l, here, s.team\[idx\], own);|        mrui::ui_team_row(l, sizeof l, false, s.team[idx], own);|'
  # ⛔ C108 THE ROW DRAWN AT **STATUS's** NARROWED ORIGIN — under S3's reserved 24x24 mark. 19 columns at x = 40 also
  #    runs off the right edge, so this is the §7.1 clip the whole width discipline exists to forbid.
  ctl "C108 the team rows are ALSO drawn at the STATUS x=40 origin" yes \
      's|        mrui::ui_team_row(l, sizeof l, here, s.team\[idx\], own);|        mrui::ui_team_row(l, sizeof l, here, s.team[idx], own); status_text(row, l);|'
  # ⛔⛔ C109 THE §1.9 F-8 FIX REMOVED — the PRE-EXISTING defect, restored. Nothing else in the tree invalidates on a
  #     body row, so a lit TEAM screen goes back to sitting on a stale age until an unrelated event repaints it.
  ctl "C109 the S4 repaint invalidation is never called (F-8 re-opened)" yes \
      's|    (void)mrui::ui_team_invalidate(s_model, s, s_frame_snap);|    ;|'
  # ⛔⛔ C110 IS THE [[B226]] TAUTOLOGY SHAPE, and it is the one a review would wave through: the rule is called, with
  #     the LIVE snapshot on both sides. It can never differ, so it never raises — a correct rule wired to itself.
  ctl "C110 the invalidation compares the live snapshot with ITSELF" yes \
      's|    (void)mrui::ui_team_invalidate(s_model, s, s_frame_snap);|    (void)mrui::ui_team_invalidate(s_model, s, s);|'
  # ⛔ C111 IS THE OTHER DIRECTION, and without it every "the age turns" check above would be satisfied by a renderer
  #    that simply repainted for ever: the tempting one-line cure for staleness, which costs the panel its 2 Hz
  #    ceiling and the node its idle. Only the "no frame while every token holds" checks can see it.
  ctl "C111 the tick marks the model dirty unconditionally instead" yes \
      's|    (void)mrui::ui_team_invalidate(s_model, s, s_frame_snap);|    s_model.mark_dirty();|'
  # ⛔⛔ C112 THE ORDERING, AND IT IS A RULING (§9 R-2: KEEP `rt_team_at` order; S7 stays deferred). A re-sort is the
  #     most tempting "improvement" on this screen — worst-first, freshest-first, alphabetical — and NO width, format
  #     or count check can see one. Only P18a's per-row bytes can, which is why the three teammates carry three
  #     different labels AND three different ages. ⓘ Reversal is the cheapest expression of "the rows moved"; the
  #     defect it stands for is any sort at all.
  ctl "C112 the roster is drawn in REVERSE order (a re-sort by any other name)" yes \
      's|        mrui::ui_team_row(l, sizeof l, here, s.team\[idx\], own);|        mrui::ui_team_row(l, sizeof l, here, s.team[s.team_shown - 1 - idx], own);|'

  # ============================================================== §UI-17 S5: C113-C118, THE LOCATION COLUMNS' SEAM
  # ★★★★ THE HANDOFF AGAIN, ONE COLUMN OVER, AND IT IS THE ONE THING `--target=uigeo`, `--target=uiteam` AND BOTH
  #   native suites STRUCTURALLY CANNOT SEE: all of them call the pure units directly. What lives ONLY in
  #   `src/firmware_ui.cpp` is the PUBLISH SITE (the `peer_loc_find` read, under the hash the label resolved) and the
  #   two arguments the renderer hands the row. Get any of that wrong and every native case stays green, all 16
  #   `uigeo` + 20 `uiteam` mutations stay RED, and the panel draws a distance that belongs to nobody. P19 is the only
  #   witness in the tree.
  # ⛔ C113 IS THE S3 DEFECT'S SHAPE, RE-ADDED DELIBERATELY SO IT CANNOT COME BACK QUIETLY: read the fix LIVE in the
  #    renderer instead of from the frozen snapshot. `draw_team_screen` runs ONCE PER OLED PAGE, so a `cfg set lat`
  #    between two of the eight replays draws half the roster against the old position and half against the new one.
  #    ⛔ Every native case and every mutation stays GREEN against it — P19c is the only thing that sees it.
  ctl "C113 the own fix is read LIVE in the renderer (the rows can TEAR mid-frame)" yes \
      's|    const mrui::GeoFix own = mrui::ui_geo_fix_of(s);|    const MESHROUTE_NS::NodeConfig\& lc_ = g_node.config(); const mrui::GeoFix own{ mrui::ui_status_have_fix(lc_.lat_e7, lc_.lon_e7), lc_.lat_e7, lc_.lon_e7 };|'
  # ⛔⛔ C114 THE PUBLISH SITE SILENCED. The cache read never happens, so every row blanks — which is exactly what a
  #     correct build shows for a peer nobody has heard a position from, and is therefore invisible to every check
  #     that is not asserting a POSITIVE row.
  ctl "C114 the cache is never read, so no teammate ever has a position" yes \
      's|        if (hash != 0) {|        if (false) {|'
  # ⛔⛔⛔ C115 IS THE STALE-NOT-BLANK DEFECT, AT THE PUBLISH SITE: the age is re-derived as "now" instead of carried
  #      verbatim from the accessor, so a position from last week renders as a current one. ⛔ It is the single most
  #      dangerous thing this slice can get wrong — the operator walks toward a number that was true ten minutes ago.
  ctl "C115 the location age is published as ZERO, so a stale position renders as current" yes \
      's|            r.peer_loc_valid = g_node.peer_loc_find(hash, r.peer_lat_e7, r.peer_lon_e7, r.peer_loc_age_s, src);|            r.peer_loc_valid = g_node.peer_loc_find(hash, r.peer_lat_e7, r.peer_lon_e7, r.peer_loc_age_s, src); r.peer_loc_age_s = 0;|'
  # ⛔ C116 THE TWO COORDINATES CROSSED at the publish site — a copy-paste away, and it keeps every blank/shown
  #    decision, every width and every count correct while naming a place on the other side of the world.
  ctl "C116 latitude and longitude are published crossed" yes \
      's|            r.peer_loc_valid = g_node.peer_loc_find(hash, r.peer_lat_e7, r.peer_lon_e7, r.peer_loc_age_s, src);|            r.peer_loc_valid = g_node.peer_loc_find(hash, r.peer_lon_e7, r.peer_lat_e7, r.peer_loc_age_s, src);|'
  # ⛔⛔ C117 IS THE "SUCCESS THAT ISN'T" SHAPE ([[meshroute-id-to-hash-trust-model]]): the find's own answer is
  #     discarded and the row is published VALID regardless, so a cache MISS renders from the untouched `(0,0)` —
  #     a plausible-looking distance to the Gulf of Guinea for a teammate whose position nobody holds.
  ctl "C117 the cache miss is published as a hit (0,0 becomes a position)" yes \
      's|            r.peer_loc_valid = g_node.peer_loc_find(hash, r.peer_lat_e7, r.peer_lon_e7, r.peer_loc_age_s, src);|            (void)g_node.peer_loc_find(hash, r.peer_lat_e7, r.peer_lon_e7, r.peer_loc_age_s, src); r.peer_loc_valid = true;|'
  # ⛔⛔⛔ C118 IS THE WHOLE §3.4 PROHIBITION, AS CODE: the panel ASKS for the position it does not have. It is the
  #      most tempting "improvement" on this screen and it is ruled out — a continuously-refreshed teammate position
  #      is a separate future specification covering airtime, privacy, authentication and user control. P19b's two
  #      REAL counters (the DeviceHal queue and the radio's start count) are what make the rule measurable rather
  #      than a promise.
  ctl "C118 the renderer transmits a request for a peer it has no position for" yes \
      's|        mrui::ui_team_row(l, sizeof l, here, s.team\[idx\], own);|        mrui::ui_team_row(l, sizeof l, here, s.team[idx], own); if (!s.team[idx].peer_loc_valid) { const uint8_t f_[8] = {0}; MESHROUTE_NS::TxParams p_; p_.sf = 8; (void)g_hal.tx(f_, sizeof f_, p_); }|'

  # ==================================================================================== §UI-17 S8: C119-C120, THE WAKE
  # ★★★★ THE [[B226]] SEAM ONE MORE TIME, AND THE SPEC NAMES IT: *"a control that hard-wires the wake must redden"*.
  #   `test_firmware_ui_send.cpp` proves which push the PURE router lets through and `--target=uisend` proves the `enc`
  #   gate is load-bearing — but a wake wired into `mr_ui_on_push`, the DEVICE entry point, leaves every one of those
  #   instruments green (§B115: this TU is compiled by neither the native suite nor the simulator). P20's two negative
  #   arms are the only thing in the tree that can see it.
  # ⛔ C119 IS WAKE-ON-ANY-PUSH, i.e. the ruling's scope discarded at the one layer that can discard it silently: the
  #    cleartext post and every other kind then light a dark panel, which is exactly §8.15's rule broken.
  ctl "C119 the wake is hard-wired into the device push entry (wake-on-any-push)" yes \
      's|    const uint32_t now = uint32_t(g_hal.now());|    const uint32_t now = uint32_t(g_hal.now()); s_model.on_msg_wake(now);|'
  # ⛔ C120 IS THE OTHER DIRECTION and it is why the positive arms are not decoration: route the RX kinds through the
  #    SEND half — the "one router, surely" simplification — and the panel never wakes for a message at all, while the
  #    pure recv unit stays perfectly correct and perfectly unreached.
  ctl "C120 the RX kinds are routed to the SEND half, so no message ever wakes the panel" yes \
      's|            char who\[mrui::kLabelCap + 1\]; label_for_origin(pu, who, uint8_t(sizeof who));|            char who[mrui::kLabelCap + 1]; label_for_origin(pu, who, uint8_t(sizeof who)); (void)who; (void)mrui::ui_route_send_push(s_tracker_emg, s_tracker_normal, s_model, pu, now); if (true) break;|'

  # ================================================================================= §UI-17 S6: C121-C122, THE MARK
  # ★★★★ THE ASSET IS PURE AND ITS BYTES ARE PINNED NATIVELY (`test_firmware_ui_chrome.cpp`) AND ATTACKED BY
  #   `--target=icons` — but NOTHING there can see WHERE this file draws it, or WHETHER IT DRAWS THAT ASSET AT ALL.
  #   §B115 again: `src/firmware_ui.cpp` is compiled by neither the native suite nor the simulator. C96/C99 already
  #   cover "never drawn" and "drawn on every screen"; these two cover the two ways a drawn mark can still be wrong.
  # ⛔ C121 IS THE SLOT ABANDONED: the artwork lands at the TEXT origin, straight through rows 0-2. It is the exact
  #    edit redesign-note §4.1 reserved a permanent slot to forbid, it leaves every native case and every `icons`
  #    mutation green, and on glass it prints `TEAM ……` over the mark. P14a's exact-rect term is the witness.
  ctl "C121 the mark is drawn at the STATUS text origin (through rows 0-2)" yes \
      's|    mrui::draw_bitmap(kStatusMarkX, kStatusMarkY, kStatusMarkW, kStatusMarkH, mrui::icons::kMarkMeshRoute);|    mrui::draw_bitmap(kStatusTextX, kStatusMarkY, kStatusMarkW, kStatusMarkH, mrui::icons::kMarkMeshRoute);|'
  # ⛔ C122 IS THE WRONG ASSET AT THE RIGHT PLACE — a copy-paste from the strip's draw calls two screens up. Every
  #    coordinate is correct, a 24x24 record appears in the census, and the panel shows a 24x24 field of garbage
  #    (a 7-px glyph's 14 bytes read as 72). ⇒ this is what makes P14a's POINTER-IDENTITY term load-bearing rather
  #    than decoration; without it "a 24x24 bitmap at 12,12" is satisfied by any pointer at all.
  ctl "C122 the mark slot is drawn with a STRIP glyph (right place, wrong bytes)" yes \
      's|    mrui::draw_bitmap(kStatusMarkX, kStatusMarkY, kStatusMarkW, kStatusMarkH, mrui::icons::kMarkMeshRoute);|    mrui::draw_bitmap(kStatusMarkX, kStatusMarkY, kStatusMarkW, kStatusMarkH, mrui::icons::kIconBattery);|'
  # ⛔⛔ C123/C124 ARE THE **INCOMPLETE SWAP**, and they exist because S6 REMOVED a draw: `body_rects_on_page` used to
  #     be reddened on both arms of its split by S3's placeholder (C96 took it away, C99 spread it everywhere), and a
  #     check whose only control disappeared with the code it watched is negative space. ⇒ the placeholder comes back
  #     as a MUTATION, in the two places a half-done swap leaves it. ⓘ It is the likeliest S6 defect of all: adding
  #     the bitmap is the visible half of the job, deleting the rect is the half nobody looks at.
  ctl "C123 S6's swap is HALF DONE: the placeholder rect is still drawn behind the artwork" yes \
      's|    mrui::draw_bitmap(kStatusMarkX, kStatusMarkY, kStatusMarkW, kStatusMarkH, mrui::icons::kMarkMeshRoute);|    mrui::draw_rect(kStatusMarkX, kStatusMarkY, kStatusMarkW, kStatusMarkH);\n    mrui::draw_bitmap(kStatusMarkX, kStatusMarkY, kStatusMarkW, kStatusMarkH, mrui::icons::kMarkMeshRoute);|'
  # ⛔ C124 IS THE SAME LEFTOVER ONE LAYER OUT — "show the reservation on every screen while the art is interim" —
  #    and it is the ONLY control that can redden *an ordinary screen's body draws no rect of its own*, which is the
  #    other arm of the census split. Without it that term measures nothing at all.
  ctl "C124 the leftover placeholder drifts into the chrome (every screen shows an empty reserved box)" yes \
      's|    mrui::draw_hline(0, kBarRuleY, 128);|    mrui::draw_hline(0, kBarRuleY, 128);\n    mrui::draw_rect(kStatusMarkX, kStatusMarkY, kStatusMarkW, kStatusMarkH);|'

  # C125-C132 ★★★★ §CHROME-5 — THE STRIP'S SIXTH SLOT, THE DUTY GAUGE. Each is a plausible edit that leaves the WHOLE
  #   native suite green (the projection is pure and correct in every one of them), every `--target=chrome` and
  #   `--target=icons` mutation still RED, and every earlier control green too: what they break lives in THIS file —
  #   which x the gauge lands on, which picture each bucket selects, and WHICH ACCESSOR the snapshot publishes.
  # ⛔ C125 THE SLOT MOVED. §3.1's amendment freezes the gauge at x = 83..89 with one-pixel gaps on both sides; two
  #    pixels right and it still FITS (81 < 84, 90 < 91) and still draws, so the build-time `strip_slots_fit()` cannot
  #    see it and only a coordinate-level assertion can. That is exactly why P13/P24 restate the coordinates.
  ctl "C125 the duty gauge's slot is moved (the amendment's frozen x redefined)" yes \
      's|    /\* duty \*/ { 83, kNoToken, 89 },|    /* duty */ { 84, kNoToken, 90 },|'
  # ⛔⛔ C126 THE SAFETY-SHAPED ONE, and it is the §4.4 crossed-key defect one slot over: a node with NO duty limit
  #     drawn as one that has spent none of its budget. Both pictures are "nothing is wrong"; only one of them is true.
  ctl "C126 a node with no duty limit draws the EMPTY gauge (no limit collapsed into 0 % used)" yes \
      's|        case mrui::DutyGauge::disabled: return mrui::icons::kIconDutyDisabled;|        case mrui::DutyGauge::disabled: return mrui::icons::kIconDutyFill[0];|'
  # ⛔⛔ C127 THE OTHER SAFETY-SHAPED ONE: 100 % drawn as the plain full gauge. It is right about the level and silent
  #     about the consequence — the radio is REFUSING to transmit — which is the one duty fact an operator acts on.
  ctl "C127 the duty-BLOCKED state loses its warning mark (100 % looks like 99 %)" yes \
      's|        case mrui::DutyGauge::blocked:  return mrui::icons::kIconDutyBlocked;|        case mrui::DutyGauge::blocked:  return mrui::icons::kIconDutyFill[mrui::icons::kDutyFillLevels - 1];|'
  # ⛔ C128 THE LEVEL IGNORED — the indexed draw pinned at one picture. Every count stays identical and a gauge is
  #    drawn on every page; only a per-level assertion across the ramp sees that it never moves.
  ctl "C128 every fill level draws the same picture (the bucket's level ignored)" yes \
      's|    return mrui::icons::kIconDutyFill\[mrui::ui_duty_fill_level(d)\];|    return mrui::icons::kIconDutyFill[0];|'
  # ⛔⛔ C129 THE GAUGE READ **LIVE** IN THE RENDERER — the "the value is right there" edit, one slot's worth of C61.
  #     It is correct on any single page and TEARS across the eight replays of one frame, which only P24g can see.
  ctl "C129 the gauge is read live from the node instead of the frozen bucket" yes \
      's|    draw_strip_icon(slot(Strip::duty), duty_glyph(c.duty));|    draw_strip_icon(slot(Strip::duty), duty_glyph(mrui::ui_duty_bucket(g_node.duty_status().enabled, g_node.duty_status().pct)));|'
  # ⛔ C130/C131 THE PUBLISH SITE. Neither touches the pure unit, so every native case and every chrome mutation stays
  #    exactly as green/red as before: the snapshot simply carries a fact the node never reported.
  ctl "C130 the utilization is never published (the gauge is empty at every percentage)" yes \
      's|        s.duty_pct         = duty.pct;|        s.duty_pct         = 0;|'
  ctl "C131 the duty limit is published as always-on (an unlimited node claims a utilization)" yes \
      's|        s.duty_enabled     = duty.enabled;|        s.duty_enabled     = true;|'
  # ⛔⛔⛔ C132 THE **WRONG AUTHORITY**, which is the defect this slot was briefed against by name: the FIVE-MINUTE
  #     anti-spam basis (`channel_duty_budget_ms`, MF1/MF8) instead of the rolling-window duty status. Both are called
  #     "duty" in the codebase, both are non-zero exactly when duty is configured, and they answer DIFFERENT questions
  #     — so the mutant's gauge is plausible, stable, and unrelated to the airtime the node has actually spent.
  ctl "C132 the gauge is driven by the five-minute anti-spam budget, not duty_status()" yes \
      's|        s.duty_pct         = duty.pct;|        s.duty_pct         = uint8_t(g_node.channel_duty_budget_ms() / 1000u);|'
  # ⛔⛔ C133 THE MOVE ONLY HALF APPLIED — the sixth slot added and ONE earlier slot left on its §CHROME-3 x. It is the
  #     likeliest §CHROME-5 defect of all (adding the gauge is the visible half of the job; re-anchoring five slots
  #     that already worked is the half nobody looks at), it still satisfies `strip_slots_fit()` — home's right edge
  #     53 is still below people's 54 — and it is the ONE mutation that reddens P24f's negative space: §CHROME-3's
  #     superseded coordinates must draw NOTHING.
  ctl "C133 the home slot keeps its superseded x (the re-anchoring only half done)" yes \
      's|    /\* home \*/ { 27,      35,  52 },|    /* home */ { 28,      36,  53 },|'

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
  # ⚠ RE-ANCHORED 2026-08-23 (§UI-16 N2), the same way and for the same reason as C88 above; meaning unchanged.
  # ⚠ RE-ANCHORED AGAIN 2026-08-23 (§UI-16 N4): the call gained the FOURTH child predicate and now wraps onto
  #   two lines, so both controls anchor on the CALL line alone (a `sed` s||| cannot match across a newline).
  #   Measured, not assumed: the stale anchors matched NOTHING and the run reported both as VACUOUS.
  # ⚠ RE-ANCHORED AGAIN 2026-08-25 (§UI-16 K6), the same way and for the same reason as C88 above; meaning unchanged.
  ctl "L10 the parent row is hidden on a build that HAS a child" yes \
      's|        mrui::provision_has_child(s.prov_create_team, s.prov_join_static, s.prov_join_team, s.prov_invite, s.prov_saved_keys);|        false;|'

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

  # ==================================================================== §UI-16 N2: N1-N8, THE READ-ONLY NEARBY SCAN
  # ★★★★ THE CONTROLS FOR THE PROJECTION AND THE SCAN'S RENDERER ARM, AND THEY EXIST ONLY HERE for the reason L1-L22
  #   do: the screen is unreachable on the `l2` arm, and `src/firmware_ui.cpp` is compiled by NO other gate. The pure
  #   decisions have their own batteries (`--target=uinearby` / `--target=uinearbyrow` / `--target=model`); what NOTHING
  #   else in the tree can see is whether THIS file PUBLISHES the cache correctly and DRAWS the frozen copy of it.
  # ⛔ N4 IS THE ONE THAT MATTERS MOST — spec §3 P-5b / ruling R-13 rule 1. It is written as the two-line "while we're
  #   here" edit a reviewer would wave through (the advertiser's id IS in scope at the observation site, and this file
  #   already owns a name resolver), and it turns a TEAM's identity into a NODE's self-asserted, mutable label.
  ctl "N1 the projection is never published (an audible team reads as an empty band)" yes \
      's|        s.nearby_n = (seen > mrui::kMaxNearbyRows) ? mrui::kMaxNearbyRows : seen;|        s.nearby_n = 0; (void)seen;|'
  ctl "N2 the age is published as the raw ARRIVAL STAMP instead of the elapsed time (every row reads old)" yes \
      's|            r.age_ms    = r.age_valid ? (seen_now - e->last_ms) : 0;|            r.age_ms    = e->last_ms;|'
  ctl "N3 the ring is walked BACKWARDS (strongest/newest first) — the ruled first-observed order is gone" yes \
      's|            const MESHROUTE_NS::TeamSeen\* e = g_node.team_seen_at(i);|            const MESHROUTE_NS::TeamSeen* e = g_node.team_seen_at(uint8_t(s.nearby_n - 1 - i));|'
  ctl "N4 the advertiser NODE NAME is resolved into the row and drawn as the TEAM (R-13 rule 1)" yes \
      's|            r.snr_q4    = e->snr_q4;                  |            r.snr_q4    = e->snr_q4; r.reserved = e->src_id;   |
       s|                else        mrui::ui_fmt_nearby_row(label, sizeof label, r.team);|                else        label_for_team_id(r.team.reserved, label, uint8_t(sizeof label));|'
  ctl "N9 the TEAM ID is treated as a peer key hash and drawn through the name resolver (a third hash spelling)" yes \
      's|                else        mrui::ui_fmt_nearby_row(label, sizeof label, r.team);|                else        label_from_hash(r.team.team_id, label, uint8_t(sizeof label));|'
  # ⛔⛔ N10 IS THE ZERO-TX RULE'S OWN CONTROL, in C118's shape one screen over (spec §3 P-4): *"the scan transmits
  #   nothing"* is a PROMISE until something that DOES transmit is shown to break it. The tempting wrong fix is a
  #   liveness ping — *"is that team still there?"* — and it is exactly what a read-only observation may not do.
  ctl "N10 the scan pings each observed team to refresh it (a read-only scan that transmits)" yes \
      's|                else        mrui::ui_fmt_nearby_row(label, sizeof label, r.team);|                else { mrui::ui_fmt_nearby_row(label, sizeof label, r.team); const uint8_t f_[8] = {0}; MESHROUTE_NS::TxParams p_; p_.sf = 8; (void)g_hal.tx(f_, sizeof f_, p_); }|'
  ctl "N5 the scan draws the LIVE snapshot array instead of the model FROZEN copy (R-10, and the own-team row returns)" yes \
      's|            const mrui::NearbySelList list = mrui::nearby_sel_rows(st.nearby);|            const mrui::NearbySelList list = mrui::nearby_sel_rows(mrui::nearby_capture(s.nearby, s.nearby_n, 0));|'
  ctl "N6 F-1's honest second line is never drawn (the panel claims a general PHY scan)" yes \
      's|            body_text(2, mrui::kNearbyLeafLine);|            ;|'
  ctl "N7 the empty note is drawn but the list is not moved down (the note and BACK collide)" yes \
      's|            if (note\[0\]) { body_text(top, note); ++top; }|            if (note[0]) body_text(top, note);|'
  ctl "N8 the JOIN TEAM child is published as absent on a build that has it" yes \
      's|    s.prov_join_team   = (MR_N_LAYERS < 2) \&\& (MR_FEAT_TEAM != 0);|    s.prov_join_team   = false;|'

  # ============================================== §UI-16 N3: N11-N15, THE `JOIN <fingerprint>?` CONFIRMATION SCREEN
  # ★★★★ THE CONTROLS FOR THE CONFIRMATION AND FOR THE RESULT ARM'S NEW OUTCOME, and they exist ONLY here for the
  #   reason L1-L22 and N1-N10 do: the screen is unreachable on the `l2` arm and `src/firmware_ui.cpp` is compiled by
  #   NO other gate. The pure decisions have their own batteries (`--target=uiprov` for the act, `--target=model` for
  #   the flow, `--target=uinearbyrow` for the title's bytes); what nothing else in the tree can see is whether THIS
  #   file draws the SELECTED team, with the JOIN pair, and whether the RESULT screen learned about `team_joined`.
  # ⛔ N13 IS THE ONE THAT MATTERS MOST — spec §4-N3 pin 2 / §3 P-7 arriving through the RENDERER's door: `s.team_id`
  #   is one field away, it is what the CREATE confirmation legitimately draws (its `REPLACES` line), and it turns
  #   the question *"join THIS team?"* into a confirmation about the team we are LEAVING. ⓘ Every native case stays
  #   green: the id the ACT carries is untouched — only the six characters the operator reads are wrong.
  # ⚠ THE MARKER IS SPELLED `char(62)` / `char(32)`, ⛔ never a quoted character: a control label and its sed script
  #   are single-quoted shell words ([[B227]]), so an apostrophe inside one would end the quoting.
  ctl "N11 the join confirmation is drawn with the CREATE title" yes \
      's|            mrui::ui_fmt_nearby_join_title(title, sizeof title, st.nearby_sel_id);|            snprintf(title, sizeof title, "%s", mrui::kProvCreateTitle);|'
  ctl "N13 the confirmation names the team we are LEAVING, not the selected one" yes \
      's|            mrui::ui_fmt_nearby_join_title(title, sizeof title, st.nearby_sel_id);|            mrui::ui_fmt_nearby_join_title(title, sizeof title, s.team_id);|'
  ctl "N12 the confirmation re-spells its actions with the CREATE pair (S-9 not called)" yes \
      's|            body_text(0, title);|            body_text(0, title); snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::back) ? char(62) : char(32), mrui::prov_confirm_label(mrui::ProvConfirm::back)); body_text(3, l); snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::confirm) ? char(62) : char(32), mrui::prov_confirm_label(mrui::ProvConfirm::confirm)); body_text(4, l); return;|'
  ctl "N14 the confirmation opens with JOIN marked instead of BACK (P-13 inverted)" yes \
      's|            body_text(0, title);|            body_text(0, title); snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::confirm) ? char(62) : char(32), mrui::join_confirm_label(false)); body_text(3, l); snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::back) ? char(62) : char(32), mrui::join_confirm_label(true)); body_text(4, l); return;|'
  # ⛔⛔ N15 IS THE HALF-DONE SLICE: the outcome is added, the model carries it, the words are right — and the RESULT
  #   arm still tests only for `created`, so a join shows its headline and NEITHER identity. It is the exact shape a
  #   reviewer waves through, and only a per-ROW assertion at the exact coordinate sees it.
  ctl "N15 the result screen never learned about team_joined (no id, no fingerprint)" yes \
      's|                st.prov_answer.outcome == mrui::UiProvOutcome::team_joined) {|                false) {|'
  # ⛔⛔ N16 IS THE FORBIDDEN LEXEME ARRIVING THROUGH THE RENDERER'S DOOR (S-33), and it exists so that P22c's
  #   *"no banned lexeme is on the panel"* is a MEASUREMENT rather than negative space: the native suite pins the
  #   VOCABULARY of `prov_result_head`/`_detail`, but nothing there can stop this file adding a fourth row of its
  #   own. It is the reassuring line a reviewer would add ("say what the operator now is"), and §3.6.4 `:815` bans
  #   exactly that word for exactly this state.
  ctl "N16 the join result adds a KEYLESS row of its own (S-33, straight into the renderer)" yes \
      's|                body_text(2, fp);|                body_text(2, fp); if (st.prov_answer.outcome == mrui::UiProvOutcome::team_joined) body_text(3, "KEYLESS");|'

  # ======================================== §UI-16 K5: K5a-K5c, THE `SAVED KEY FOUND` OFFER's RENDERER ARM
  # ★★★★ THE CONTROLS FOR THE OFFER SCREEN, and they exist ONLY here for the reason every N- and L- control above
  #   does: the screen is unreachable on the `l2` arm and `src/firmware_ui.cpp` is compiled by NO other gate. The
  #   pure decisions have their own batteries (`--target=teamkeyring` for the activation, `--target=uiprov` for the
  #   mapping, `--target=model` for the flow and the two lexemes); what NOTHING else in the tree can see is whether
  #   THIS file draws the ruled title, an IDENTITY beside it, and the two actions with BACK on the safe row.
  # ⛔⛔ K5a IS THE ONE THAT MATTERS MOST: the row the operator reads as `BACK` — the row the screen OPENS ON — is
  #   drawn with the OTHER action's word. Every native case stays green (the model's `prov_confirm` is untouched and
  #   the act is still gated on it); only the six characters under the marker are wrong, and pressing what the panel
  #   calls BACK installs a stored secret. That is P-13 defeated at the one door where the act is a KEY.
  ctl "K5a the offer draws USE SAVED KEY on the BACK row too (the safe word gone from the safe arm)" yes \
      's|                     mrui::saved_key_label(mrui::ProvConfirm::back));|                     mrui::saved_key_label(mrui::ProvConfirm::confirm));|'
  # ⛔ K5b — THE OFFER WITH NO IDENTITY ON IT: the operator is asked whether to install "the saved key" with nothing
  #   on the glass saying WHICH team's. §3 P-7c's rule is that the identity stays visible at the moment of an
  #   irreversible act, and installing a content key is one.
  ctl "K5b the offer draws no team identity at all (which team is this key for?)" yes \
      's|            mrui::ui_fmt_team_fingerprint(fp, sizeof fp, st.saved_key_team);|            fp[0] = 0;|'
  # ⛔ K5c — THE RULED TITLE REPLACED BY A LANDED ONE (S-28 not called). It is the shape N11 attacks one screen over:
  #   a title that reads plausibly and names the wrong operation entirely.
  ctl "K5c the offer is drawn with the CREATE title instead of S-28" yes \
      's|            body_text(0, mrui::kSavedKeyTitle);|            body_text(0, mrui::kProvCreateTitle);|'

  # ================================================ §UI-16 N4: O1-O9, THE `INVITE MEMBER` WINDOW's RENDERER ARM
  # ★★★★ THE CONTROLS FOR THE WINDOW's THREE SCREENS AND FOR THE MEMBER PROJECTION, and they exist ONLY here for the
  #   reason every N- and L- control above does: the screens are unreachable on the `l2` arm and `src/firmware_ui.cpp`
  #   is compiled by NO other gate. The pure decisions have their own battery (`--target=uiinvite`) and the flow has
  #   `--target=model`; what NOTHING else in the tree can see is whether THIS file publishes the members correctly
  #   and draws the LIVE list, the TEAM's fingerprint and the FROZEN full hash.
  # ⛔ O8 IS THE ONE THAT MATTERS MOST — spec §4-N4 / F-15's named refusal, arriving through the PUBLISH site's door:
  #   `label_from_hash` is one call away, it is what the TEAM screen legitimately uses, and it looks like reuse (U1!)
  #   — but its `0x%08lx` fallback clamped into six columns is a THIRD spelling of the hash beside the full id and
  #   the fingerprint, on the very row whose job is to be readable. Every native case and every `uiinvite` mutation
  #   stays green against it: the pure unit renders faithfully whatever it is handed.
  # ⛔ O9 IS P-7d THROUGH THE RENDERER's DOOR: the frozen selection is one field away from the LIVE row under the
  #   cursor, and a refresh between the two presses is exactly what F-14 says may not move the target.
  ctl "O1 the window draws a FROZEN list instead of the live members (no local refresh, F-14)" yes \
      's|            const mrui::InviteSelList ilist = mrui::invite_sel_rows(st.invite, s.member, s.team_shown);|            const mrui::InviteSelList ilist = mrui::invite_sel_rows(st.invite, nullptr, 0);|'
  ctl "O2 the window heads itself with the TEAM label instead of its fingerprint (F-3 / S-36)" yes \
      's|            mrui::ui_fmt_team_fingerprint(fp, sizeof fp, s.team_id);|            label_for_team_id(s.my_team_id, fp, uint8_t(sizeof fp));|'
  ctl "O3 every candidate row is drawn UNMARKED — the cursor is invisible on the one list that grants a key" yes \
      's|                else        mrui::ui_fmt_invite_row(label, sizeof label, marker, r.cand);|                else        mrui::ui_fmt_invite_row(label, sizeof label, char(32), r.cand);|'
  ctl "O4 the note row is drawn but the list is not moved down (the note and the first candidate collide)" yes \
      's|                body_text(uint8_t(3 + row), label);|                body_text(uint8_t(2 + row), label);|'
  ctl "O5 the confirmation is drawn with the six-column selection aid instead of the FULL hash (P-7c)" yes \
      's|            mrui::ui_fmt_member_hash_full(hash, sizeof hash, st.invite.sel_hash);|            mrui::ui_fmt_member_fingerprint(hash, sizeof hash, st.invite.sel_hash);|'
  ctl "O6 the confirmation shows the cached NAME instead of the hash — a mutable label as the only identity (P-7c)" yes \
      's|            mrui::ui_fmt_member_hash_full(hash, sizeof hash, st.invite.sel_hash);|            label_from_hash(st.invite.sel_hash, hash, uint8_t(sizeof hash));|'
  ctl "O7 the INVITE child is published as absent on a build and a node that have it" yes \
      's|    s.prov_invite      = (MR_N_LAYERS < 2) \&\& (MR_FEAT_TEAM != 0) \&\& (g_node.config().team_id != 0);|    s.prov_invite      = false;|'
  ctl "O8 the member name is published through label_from_hash — the truncated 0x third spelling (F-15)" yes \
      's|            const uint8_t nn = g_node.peer_name_find(hash, mem.name, uint8_t(sizeof mem.name - 1));|            label_from_hash(hash, mem.name, uint8_t(sizeof mem.name)); const uint8_t nn = 0; (void)nn;|
       s|            mem.name\[nn\] = .\\\\0.;|            ;|'
  ctl "O9 the confirmation re-reads the row under the cursor instead of the FROZEN selection (F-14 / P-7d)" yes \
      's|            mrui::ui_fmt_member_hash_full(hash, sizeof hash, st.invite.sel_hash);|            mrui::ui_fmt_member_hash_full(hash, sizeof hash, s.member[st.cursor].key_hash32);|'

  # ====================================================== §UI-16 N5: O10-O16, REQUEST PUBKEY's DEVICE/RENDERER SEAMS
  # ★★★ O10, O11 AND O16 ARE THE FORWARDS NO PURE TEST CAN SEE. The typed carrier, plane, hash correlation and grant
  #   floor are mutation-covered in `firmware_ui_invite.h` / `firmware_ui_model.h`; only this host-compiled probe can
  #   prove that the production TU really hands the command to its existing executor and really taps the incoming
  #   cached-key push into the model. Both tempting omissions leave every native case green.
  ctl "O10 ★★ the reqpubkey device forward DROPS the real executor call (the panel waits after airing nothing)" yes \
      's|        const mrfw::ExecResult r = mrfw::exec_command(line, n);|        mrfw::ExecResult r{}; r.ok = true;|'
  ctl "O11 ★★ the peer_key_cached push is never forwarded to the invite model (WAITING can never complete)" yes \
      's|            s_model.on_invite_push(pu);     // §UI-16 N5 — pure hash correlation + grant-bar recheck|            ;|'
  ctl "O12 NEED PUBKEY is rendered as WAITING before the operator has authorised a request" yes \
      's|            body_text(0, mrui::kInviteNeedPubkey);|            body_text(0, mrui::kInviteWaitingPubkey);|'
  ctl "O13 the waiting screen prints the forbidden ambiguous words WAITING FOR KEY (S-34)" yes \
      's|            body_text(0, mrui::kInviteWaitingPubkey);|            body_text(0, "WAITING FOR KEY");|'
  ctl "O14 the NEED PUBKEY renderer marks REQUEST instead of the model default BACK" yes \
      '/case mrui::Provision::invite_need_pubkey:/,/return;/ s|st.prov_confirm == mrui::ProvConfirm::back|st.prov_confirm == mrui::ProvConfirm::confirm|'
  ctl "O15 the grant-ready renderer marks GRANT KEY instead of the model default REJECT" yes \
      '/case mrui::Provision::invite_confirm:/,/return;/ s|st.prov_confirm == mrui::ProvConfirm::invite_reject|st.prov_confirm == mrui::ProvConfirm::invite_grant|'
  # ★★★ O16 IS THE QG BLOCKER's DEVICE HALF (2026-08-24), and it is the ONE shape no pure test can reach: the
  #   forward reports `ok` honestly but INVENTS the outcome code, so every synchronous refusal reaches the pure
  #   verdict wearing `queued` and the panel claims `WAITING FOR PUBKEY` for a request the executor REFUSED.
  #   ⓘ It reddens on P23d's refusal arm, which is exactly why that arm drives the REAL forward and not a fake.
  ctl "O16 ★★ the forward INVENTS the outcome code instead of reporting the executor's (a refusal reads as queued)" yes \
      's|        out.code     = r.result.code;|        out.code     = MESHROUTE_NS::CmdCode::queued;|'

  # ====================================================== §UI-16 N6: O17-O21, THE GRANT ACT's DEVICE/RENDERER SEAMS
  # ★★★ WHAT ONLY THIS PROBE CAN SEE. The eight-arm mapping, the `{dst, ctr}` correlation and the flow all have their
  #   own batteries (`--target=uiinvite` I20-I30, `--target=model` V15-V20, `--target=uisend` U07-U09) — but ⛔ NONE of
  #   them compiles `src/firmware_ui.cpp`, so a verdict arm drawn from the wrong field, a word that ignores the state,
  #   or a device forward that quietly picks its own plane leaves every native case green while the panel says the
  #   wrong thing about a PRIVATE KEY.
  # ⛔ O19 IS THE ONE THAT MATTERS MOST: the plane is a PURE ruling (`mrui::kInviteGrantPlane`), and the tempting
  #   "helpful" edit is to hardcode it at the forward — which is exactly how `delegated` becomes reachable.
  # ⛔ O21 IS THE HANDLE LOST AT THE FORWARD: `out_ctr` is one of the two terms the whole correlation hangs on, and
  #   a forward that swallowed it leaves the panel at `GRANT QUEUED` with no promotion able to arrive.
  #   ⛔ RE-ANCHORED 2026-08-24 (§UI-16 N6b): its old headline said *"every grant reads as PARKED"* — that was the
  #   ctr-INFERENCE, which is exactly what the correction removed. The word is now the CORE's; what a swallowed
  #   handle destroys is the CORRELATION, and the check that reddens says so.
  # ⛔⛔ O23 IS THE **OTHER** TERM, AND IT IS THE N6b BLOCKER ITSELF WEARING THE SEAM'S CLOTHES: a forward that drops
  #   `out_dst` hands the panel a zero destination, so the TxDone edge the core really produces can never match and
  #   the screen waits at `GRANT QUEUED` for ever. ⛔ It is a SEPARATE control from O21 because the two terms fail on
  #   two different pushes, and either alone is enough to strand the operator.
  ctl "O17 the verdict screen draws a FIXED word instead of the outcome's own (S-24 collapsed at the renderer)" yes \
      's|            body_text(0, mrui::invite_grant_word(st.grant.st));|            body_text(0, mrui::kInviteGrantQueued);|'
  ctl "O18 the verdict draws the DISCARDED window selection instead of the verdict's own identity (P-7c)" yes \
      's|            mrui::ui_fmt_member_hash_full(hash, sizeof hash, st.grant.hash);|            mrui::ui_fmt_member_hash_full(hash, sizeof hash, st.invite.sel_hash);|'
  ctl "O19 ★★ the device forward PICKS ITS OWN PLANE instead of passing the pure unit's (delegated made reachable)" yes \
      's|        return mrfw::device_team_grant(key_hash32, plane, out_ctr, out_dst);|        (void)plane; return mrfw::device_team_grant(key_hash32, MESHROUTE_NS::Plane::AUTO, out_ctr, out_dst);|'
  ctl "O20 the confirmation shows the NAME in place of the full hash once one is cached (P-7c)" yes \
      's|            body_text(1, ident.hash);|            body_text(1, ident.name[0] ? ident.name : ident.hash);|'
  ctl "O21 ★★ the grant forward SWALLOWS the origination handle — nothing can ever correlate" yes \
      's|        return mrfw::device_team_grant(key_hash32, plane, out_ctr, out_dst);|        uint16_t ignored = 0; (void)out_ctr; return mrfw::device_team_grant(key_hash32, plane, \&ignored, out_dst);|'
  ctl "O23 ★★ the grant forward SWALLOWS the SEND-TIME resolved dst — the TxDone edge can never match (N6b)" yes \
      's|        return mrfw::device_team_grant(key_hash32, plane, out_ctr, out_dst);|        uint8_t ignored_dst = 0; (void)out_dst; return mrfw::device_team_grant(key_hash32, plane, out_ctr, \&ignored_dst);|'
  # ⛔⛔ O22 IS THE FORBIDDEN LEXEME ARRIVING THROUGH THE RENDERER'S DOOR, one screen on from N16, and it exists so
  #   that P24a's *"no completion word anywhere on the panel"* is a MEASUREMENT rather than negative space: the pure
  #   suite pins the VOCABULARY of `invite_grant_word` (and `uiinvite` I28 attacks the lexeme itself), but nothing
  #   there can stop THIS file adding a reassuring row of its own — and `JOIN COMPLETE` (S-32) is exactly the row a
  #   well-meaning author adds beside a verdict that says only `GRANT QUEUED`.
  ctl "O22 ★★ the verdict screen adds a JOIN COMPLETE row of its own (S-32, straight into the renderer)" yes \
      's|            mrui::ui_fmt_member_hash_full(hash, sizeof hash, st.grant.hash);|            mrui::ui_fmt_member_hash_full(hash, sizeof hash, st.grant.hash); body_text(3, "JOIN COMPLETE");|'
  # ⛔⛔ K1 IS THE §T3 SHAPE FOR §UI-16 K4, AND IT IS THE CONTROL THAT KEEPS P15k FROM BEING VACUOUS. The pure arm
  #   lives in `firmware_ui_send.h` (target `uisend`, entries U10-U13) and the drain-loop GATE lives in
  #   `src/fw_main.cpp` (`probe_board_ui`'s W47) — but the only thing that carries a FORWARDED receipt from
  #   `mr_ui_on_push` into that arm is the `case` label in THIS file, and no native suite or corpus compiles it.
  #   Drop the case and the push falls through to the SEND router, which answers `false` and renders nothing: the
  #   whole feature stays green everywhere else and the panel never says a word.
  ctl "K1 ★★★ the team_key_received case is dropped from mr_ui_on_push (the receipt never reaches the note)" yes \
      's|        case MESHROUTE_NS::PushKind::team_key_received:|        case MESHROUTE_NS::PushKind::hash_resolved:|'
  # ⛔ K2 IS F-3/P-5 ARRIVING THROUGH THE RENDERER'S DOOR, exactly as O22 is for S-32: the pure arm reads no label
  #   and the note carries no label FIELD, so the only way a granter's self-asserted name can reach a team's
  #   identity on this panel is a row this file adds. That is the row a well-meaning author adds.
  ctl "K2 ★★ the grant receipt's name= is drawn as a row of its own (F-3/P-5, S-36's forbidden usage)" yes \
      's|            const char\* detail2 = mrui::prov_result_detail2(st.prov_answer);|            if (st.prov_answer.outcome == mrui::UiProvOutcome::team_key_received) body_text(3, "Wolfgangetta");\n            const char* detail2 = mrui::prov_result_detail2(st.prov_answer);|'
  # ⛔⛔ K3/K4 ARE [[B243]]'s HALF, AND THEY ARE WHAT KEEPS P15k2 FROM BEING VACUOUS. The eighth hook in
  #   `lib/hal/mr_ui.h` is a DECLARATION; its body is here, and no native suite and no corpus compiles this file.
  #   The drain loop's side of the wire (the `else` itself, dropped / fired on success too / inverted) is pinned by
  #   `tools/probe_board_ui`'s W47 — six controls — because that is where the branch lives.
  # ⓘ SCOPE STATED RATHER THAN OVERCLAIMED, exactly as P15k states its own: the ⛔ NAVIGATE and ⛔ WAKE negatives of
  #   this door are attacked where they can be MEASURED — `UiModel::on_team_key_note` is the single entry point both
  #   doors share (U1), so `--target=model` V23/V24 attack them directly and `test/test_firmware_ui_send.cpp` drives
  #   the failure arm against a DARK model. A wake control HERE would be worthless: the panel is LIT at P15k2, so
  #   `set_power_save` could not move and the mutant would pass — an unusable control, ⛔ never a "verified" one.
  ctl "K3 ★★★ the failed-save door renders the SUCCESS verdict (a RAM-only key reads as RECEIVED)" yes \
      's|    s_model.on_team_key_note(/\*saved=\*/false, keyring_full, uint32_t(g_hal.now()));|    s_model.on_team_key_note(/*saved=*/true, keyring_full, uint32_t(g_hal.now()));|'
  ctl "K4 ★★★ the failed-save door does nothing at all ([[B243]] restored from the new end)" yes \
      's|    s_model.on_team_key_note(/\*saved=\*/false, keyring_full, uint32_t(g_hal.now()));|    (void)keyring_full;|'

  # ============================================= §UI-16 K6: K5-K10, SAVED-KEY RETENTION MANAGEMENT AT THE RENDERER
  # ★★★★ THE CONTROLS FOR THE RETENTION SCREENS' RENDERER ARMS AND FOR THE FIFTH CHILD PREDICATE, and they exist
  #   ONLY here for the reason every L/N/O control does: `src/firmware_ui.cpp` is compiled by NO other gate, so a
  #   list drawn with the wrong token, a marker wired to the wrong fact or a protected screen that grew a
  #   destructive row leaves the WHOLE native suite green (the service, the model and the adapter are each proved
  #   against their own fakes) and every mutation RED — while the panel invites the operator to destroy a key.
  # ⛔ K7 IS THE ONE THAT MATTERS MOST (spec §4-K6 pin 7): it is the "tidy the two screens up" edit a reviewer would
  #   wave through — the LIST already prints a fingerprint, so why should the confirmation print something else? —
  #   and it makes a SHORT-FINGERPRINT COLLISION able to name the wrong record on the one screen that destroys one.
  # ⚠ THE ANCHOR DELIBERATELY STOPS BEFORE THE `&&`: `&` is `sed`'s replacement metacharacter and its escaping
  #   differs between the pattern and the replacement, so the shortest SAFE anchor is the first conjunct — and
  #   falsifying it falsifies the whole expression.
  ctl "K5 ★★ the fifth child is never published, so SAVED KEYS is unreachable from the panel" yes \
      's|    s.prov_saved_keys  = (MR_N_LAYERS < 2)|    s.prov_saved_keys  = (0 != 0)|'
  ctl "K6 ★★★ the ACTIVE marker is drawn on EVERY row — the protected record is indistinguishable (S-44 collapsed)" yes \
      's|                    snprintf(l, sizeof l, "%c%s%s", marker, fp, mrui::saved_key_row_tag(r.key));|                    snprintf(l, sizeof l, "%c%s%s", marker, fp, " ACTIVE");|'
  ctl "K7 ★★★★ the IRREVERSIBLE confirmation draws the six-hex FINGERPRINT instead of the FULL id — a short-token collision can then name the wrong record on the one screen that destroys one" yes \
      's|            mrui::ui_fmt_team_id_full(fid, sizeof fid, st.forget_team);|            mrui::ui_fmt_team_fingerprint(fid, sizeof fid, st.forget_team);|'
  ctl "K8 ★★★★ the PROTECTED screen grows the destructive action row — the panel offers FORGET KEY for the ACTIVE key" yes \
      's|            body_text(2, aid);|            body_text(2, aid); body_text(4, mrui::kForgetKeyText);|'
  ctl "K9 ★★★ the removal's verdict screen draws a FIXED word instead of the outcome's own — a failed removal reads as KEY FORGOTTEN" yes \
      's|            const char\* khead = mrui::prov_result_head(st.prov_answer);|            const char* khead = mrui::kKeyForgottenText;|'
  ctl "K10 ★★★ the retention list is drawn WITHOUT its store-state note, so an unreadable keyring reads as an empty one" yes \
      's|            const char\* knote = mrui::saved_keys_head(st.saved_keys);|            const char* knote = "";|'

  # ============================== §UI-16 K7 ([[B245]]): R1-R4, THE ROSTER GRANT'S RENDERER + PUBLISH ARMS
  # ★★★★ THEY EXIST ONLY HERE for the reason every N-, L- and K- control above does: the DM act sub-view's optional
  #   row is reachable only where a provisioning child is (`prov_invite` carries `MR_N_LAYERS < 2`), and
  #   `src/firmware_ui.cpp` is compiled by NO other gate. The pure decisions have their own battery entries
  #   (`--target=model` W01-W11); what nothing else in the tree can see is whether THIS file draws the act's row set
  #   from the model's resolver, and whether it publishes the identity the SELF veto is asked at.
  # ⛔⛔ R1 IS THE HALF-DONE SLICE and the likeliest defect of all: the row's TEXT is taken from the pure unit (so the
  #   act is visibly there) while the LENGTH is taken from the canned table again — which is §B66 exactly: the
  #   optional row steals `back`'s slot and the operator's way out is off the panel.
  ctl "R1 ★★★ the act sub-view's LENGTH comes from the canned table again, so the optional row pushes the way out off the panel (§B66)" yes \
      's|    const uint8_t n     = mrui::compose_row_count(dm, grant);|    const uint8_t n     = dm ? mrui::kDmTextCount : mrui::kChannelTextCount;|'
  # ⛔⛔ R2 IS THE OTHER HALF: the length knows about the act and the TEXT does not, so the row is drawn as a SECOND
  #   `back, don't send` — a list with two identical exits, one of which is the door to a private key.
  ctl "R2 ★★★ the row TEXT is resolved without the act, so the grant row renders as a second way out" yes \
      's|                 mrui::compose_row_text(uint8_t(first + row), dm, grant));|                 mrui::compose_row_text(uint8_t(first + row), dm, false));|'
  # ⛔⛔⛔ R3 IS THE RENDERER DECIDING WHAT THE MODEL DECIDED — the "every DM has a member, so just draw it" edit. It
  #   offers the act for a ROUTE-ONLY member (no binding, no seal target) and for OURSELVES, i.e. it defeats the
  #   whole hide predicate at the one door where the act ships a private key.
  ctl "R3 ★★★★ the act's row is drawn on EVERY DM sub-view, ignoring the model's offer (the four vetoes bypassed)" yes \
      's|    const bool    grant = st.compose_grant_row;|    const bool    grant = (st.compose == mrui::Compose::dm);|'
  # ⛔⛔ R4 IS THE PUBLISH SITE, and it leaves the whole native suite green: the model still asks the SELF question,
  #   the snapshot simply never carries the answer — so a roster row that is US is offered a grant of the key to
  #   itself. ⓘ `0` is the honest "no identity yet" value, which is precisely why it is the tempting stub.
  ctl "R4 ★★★ our own stable identity is never published, so the SELF veto can never fire" yes \
      's|    s.my_key_hash32        = g_node.key_hash32();|    s.my_key_hash32        = 0;|'

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
