#!/usr/bin/env bash
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# §B278 S4 USB WIRING PROBE — the custody console line's PRODUCTION RENDERER, host-compiled and host-RUN.
#
# WHY THIS EXISTS. `platformio.ini`'s native env sets `test_build_src = no`, so neither the doctest suite nor the
# simulator compiles ANY `src/` translation unit. Before S4 the custody USB arm was three facts and one fixed
# sentence inside `fw_main.cpp`'s push switch, and calling it "board glue" was arguable. §B278 S4 gives that line
# THREE DECISIONS — direct vs translated, `target_id` vs `target_hash`, and a second fail-loud arm — so the whole
# renderer moved into `src/firmware_custody_push.h` and THIS probe compiles and EXECUTES it against the real
# custody codec through the real Arduino `Print` seam. The controls below are exactly the ways that renderer
# could go wrong; each must turn the probe RED.
#
# ★★ IT MUST REMAIN IN THE REPOSITORY AND BE COMMITTED WITH THIS SLICE, for the reason its sibling probes state
#    at this spot: this project has already LOST a proven 33-assert scenario to a session scratchpad
#    ([[meshroute-agent-scratchpad-is-volatile]]). A recipe in a note is not a storage location.
#    ⓘ Verify tracked-ness with `git ls-files`, never from this comment.
#
# FAKES — REUSED, NOT FORKED (U1): `tools/probe_console_sink/fakes/Arduino.h` and nothing else. Its `Print`
#   reproduces the real cores' CALL GRANULARITY (`println(F(x))` is two writes; `print(int)` is its own call),
#   which is the property a line-assembly bug hides behind. ⛔ A second fake is how two probes end up measuring
#   two different Arduinos.
#
# WHAT IS BEHAVIOURAL AND WHAT IS STRUCTURAL, said plainly rather than dressed up:
#   • BEHAVIOURAL — the golden lines, the target-field selection, the counters, the fail-loud arm, the `seq`
#     convention, the warning, and the EXECUTED cross-pin against the real `console_json.cpp` (the USB word and
#     the JSON word are rendered for the SAME record and compared; the two renderers share no symbol, because a
#     `lib/` TU may not depend on `src/`).
#   • STRUCTURAL — that `src/fw_main.cpp`'s switch arm DELEGATES exactly once and keeps no parser, no target
#     selection and no wording. Necessary, NOT sufficient; the behavioural half above is what makes it a gate.
#
# USAGE:  tools/probe_custody_usb/run.sh            # probe + NEGATIVE CONTROLS (the controls run BY DEFAULT)
#         tools/probe_custody_usb/run.sh --no-neg   # probe only — NOT a gate, use only while iterating
# ⚠ The controls run by default DELIBERATELY (the sibling probes' documented trap: controls described as "not
#   optional" while the standard command skipped them, so the reported gate never included them).
#
# ★★★ WHAT A CONTROL HAS TO BE. Each applies ONE mutation to a COPY of a REAL production source — the tempting
#     WRONG SHAPE, not merely a deletion — and must make the probe RED. Four ways a control can be worthless, all
#     four checked: (1) the sed matched nothing -> VACUOUS; (2) the mutant does not compile -> the probe never
#     ran; (3) the probe still passes -> the check measures nothing; (4) ⛔ [[B237]] the mutant DIED (a signal, or
#     a non-zero exit with ZERO `  FAIL ` lines) -> UNUSABLE, never "verified" — see `classify_control`.
# ⚠ And the tree must come out untouched: the real sources' md5 is captured before and asserted after.

set -uo pipefail
cd "$(dirname "$0")" || exit 1
ROOT=$(cd ../.. && pwd)          # ★ absolute — a relative path in a cwd-resetting shell silently measured nothing
HERE=$(pwd)                      #   once already ([[B82]]). Never make these relative.
CXX=${CXX:-g++}
CC=${CC:-gcc}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

HELPER="$ROOT/src/firmware_custody_push.h"   # THE RENDERER — controls C2..C9 mutate this
FWMAIN="$ROOT/src/fw_main.cpp"               # THE SWITCH    — control C1 mutates this
python3 "$HERE/observer_structure.py" "$FWMAIN" "${1:-}" || exit 1
FAKES="$ROOT/tools/probe_console_sink/fakes" # the ONE Arduino fake (U1: reused, never forked)

INCS=(-I"$FAKES" -I"$ROOT/src" -I"$ROOT/lib/core" -I"$ROOT/lib/console" -I"$ROOT/lib/hal"
      -I"$ROOT/lib/monocypher/src")
STD=(-std=gnu++20 -fno-exceptions -fno-rtti -O0)

# ================================================================================================================
# ★★★ THE TWO PINNED COUNTS — the `PIN_CASES` idiom from `tools/probe_ui_model_mutations.py`, applied here for the
#     same reason it exists there: WITHOUT A PIN, DELETING A CHECK IS INVISIBLE. A probe that reports "0 failed"
#     over 3 surviving checks is indistinguishable, in its exit code and in its summary line, from one that ran
#     all 27. Growing or trimming the probe therefore means DELIBERATELY updating a number here, with its
#     derivation.
#
# PIN_CHECKS = 27, derived by running the clean probe and counting its `  ok  `/`  FAIL ` lines:
#     U1  direct golden 1 · U2  no translated field on a direct record 1
#     U3  translated node-id golden 1 · U4 translated hash golden 1
#     U5  exactly one target field (node-id) 1 · U6 exactly one target field (hash) 1 · U7 no alias 1
#     U8  mobile_ctr is ctrM 1 · U9 mobile_ctr is never ctrH 1
#     U10 the unparseable fail-loud line 1
#     U11 seq omitted at 0 1 · U12 seq present when nonzero 1
#     U13 the warning survives 1 · U14 exactly one warning per line 1 · U15 no NACK/non-delivery wording 1
#     U16..U22 the EXECUTED JSON cross-pin (kind node_id · kind hash · hash form · target_id · mobile_ctr
#              · delegated · ctr still ctrH) 7
#     U23 fw_main read 1 · U24 delegates exactly once 1 · U25 no parser in fw_main 1
#     U26 no selection/wording in the arm 1 · U27 the helper defines the renderer once 1
#     2+2+3+2+1+2+3+7+5 = 27. ✓
# PIN_CONTROLS = 10: the [[B237]] control-of-the-controls + C1..C9.
PIN_CHECKS=27
PIN_CONTROLS=10

# ---- the tree must not move -------------------------------------------------------------------------------------
# ⛔ SPELLED ONCE, IN A FUNCTION (the sibling probe's lesson: two `cat` lists drifted apart and produced a FALSE
#    RED on a tree nothing had touched).
md5_sources() {
  cat "$HELPER" "$FWMAIN" "$HERE/probe_main.cpp" "$FAKES/Arduino.h" \
      "$ROOT/lib/console/console_json.cpp" "$ROOT/lib/core/frame_codec.cpp" | md5sum | cut -d' ' -f1
}
MD5_BEFORE=$(md5_sources)

echo "== §B278 S4 custody USB probe (the real firmware_custody_push.h + the real codec + the real console_json) =="

# ---- the support library: the REAL lib/core + lib/console, compiled ONCE ------------------------------------------
# ⓘ No -Werror on this half: `lib/core/node_hashlocate.cpp` carries a PRE-EXISTING -Wmisleading-indentation which
#   this slice does not own. The probe's OWN TU is built -Werror below.
build_support() {
  local s o
  for s in "$ROOT"/lib/core/*.cpp "$ROOT/lib/hal/device_hal.cpp" "$ROOT/lib/hal/timer_wheel.cpp" \
           "$ROOT/lib/hal/airtime_ledger.cpp" "$ROOT/lib/console/console_json.cpp"; do
    o="$OUT/sup_$(basename "$s" .cpp).o"
    "$CXX" "${STD[@]}" -Wall -Wextra "${INCS[@]}" -c "$s" -o "$o" 2>"$OUT/sup.log" || {
      echo "SUPPORT BUILD FAILED: $s"; sed 's/^/    /' "$OUT/sup.log" | head -20; return 1; }
  done
  "$CC" -std=gnu17 -O0 -I"$ROOT/lib/monocypher/src" -c "$ROOT/lib/monocypher/src/monocypher.c" \
       -o "$OUT/sup_monocypher.o" 2>/dev/null || return 1
  return 0
}

# build_probe <helper-include-dir-or-empty> <out-binary>
# ⛔ A mutated `firmware_custody_push.h` is handed in as an include DIR that SHADOWS `src/`, so a control never
#   writes to the repository.
build_probe() {
  local hdrdir=$1 bin=$2
  local pre=()
  [ -n "$hdrdir" ] && pre=(-I"$hdrdir")
  : > "$OUT/build.log"
  "$CXX" "${STD[@]}" -Wall -Wextra -Werror "${pre[@]}" "${INCS[@]}" -c "$HERE/probe_main.cpp" \
       -o "$OUT/probe_main.o" 2>>"$OUT/build.log" \
    && "$CXX" "$OUT/probe_main.o" "$OUT"/sup_*.o -o "$bin" 2>>"$OUT/build.log"
}

rc=0
if ! build_support; then echo "FAIL — the support half did not build"; exit 1; fi
if ! build_probe "" "$OUT/probe"; then
  echo "PROBE BUILD FAILED — the real src/ header did not host-compile:"
  sed 's/^/    /' "$OUT/build.log" | head -25
  exit 1
fi
# ⛔⛔ THE PIPELINE RUNS BARE AND `PIPESTATUS` IS READ ON THE VERY NEXT LINE. A trailing `|| true` would REPLACE
#     `PIPESTATUS` with `(0)` and read a failing probe as a passing one — the sibling probe's measured defect:
#         set -uo pipefail; false | tee /dev/null || true; echo ${PIPESTATUS[0]}   ->  0
#         set -uo pipefail; false | tee /dev/null;         echo ${PIPESTATUS[0]}   ->  1
"$OUT/probe" "$FWMAIN" "$HELPER" 2>&1 | tee "$OUT/probe.out"
probe_rc=${PIPESTATUS[0]}
n_checks=$(grep -cE '^  (ok|FAIL) ' "$OUT/probe.out")
n_probe_fail=$(grep -c '^  FAIL ' "$OUT/probe.out")

# ==================================================================================================================
# NEGATIVE CONTROLS
# ==================================================================================================================
n_ctl=0; n_bad=0

classify_control() {   # classify_control <exit-code> <fail-line-count> -> red | passes | abnormal | silent
  local crc=$1 fails=$2
  if   [ "$crc" -eq 0 ];    then printf 'passes'    # the mutant satisfied every check — the property is not measured
  elif [ "$crc" -ne 1 ];    then printf 'abnormal'  # a signal / abort / any exit the probe cannot produce = a crash
  elif [ "$fails" -eq 0 ];  then printf 'silent'    # "failed" without naming one failure — nothing to attribute
  else                           printf 'red'
  fi
}

verdict_of() {   # verdict_of <label> <binary> <fw_main-path> <helper-path>
  local label=$1 bin=$2 fwm=$3 hdr=$4
  local rc_m=0
  bash -c '"$1" "$2" "$3"; exit $?' _ "$bin" "$fwm" "$hdr" >"$OUT/mutant.out" 2>&1 || rc_m=$?
  local fails; fails=$(grep -c '^  FAIL ' "$OUT/mutant.out")
  local verdict; verdict=$(classify_control "$rc_m" "$fails")
  case "$verdict" in
    passes)   n_bad=$((n_bad+1)); printf '  FAIL %s — the probe still PASSES against the mutant (measures nothing)\n' "$label" ;;
    abnormal) n_bad=$((n_bad+1)); printf '  FAIL %s — the mutant DIED (exit %s, %s failure(s)); a crash measures nothing\n' "$label" "$rc_m" "$fails" ;;
    silent)   n_bad=$((n_bad+1)); printf '  FAIL %s — non-zero exit with ZERO named failures; nothing to attribute\n' "$label" ;;
    red)      n_ctl=$((n_ctl+1)); printf '  ok   %s -> RED (%s check(s) failed)\n' "$label" "$fails" ;;
  esac
}

# ctl_hdr <label> <sed-script> — mutate the RENDERER, rebuild, run.
ctl_hdr() {
  local label=$1 script=$2
  mkdir -p "$OUT/hdr"
  sed "$script" "$HELPER" > "$OUT/hdr/firmware_custody_push.h"
  if cmp -s "$HELPER" "$OUT/hdr/firmware_custody_push.h"; then
    n_bad=$((n_bad+1)); printf '  FAIL %s — the mutation changed NOTHING (VACUOUS)\n' "$label"; return
  fi
  if ! build_probe "$OUT/hdr" "$OUT/mutant.bin"; then
    n_bad=$((n_bad+1))
    printf '  FAIL %s — the mutant does not COMPILE, so the probe never ran against it:\n' "$label"
    sed 's/^/        /' "$OUT/build.log" | head -6; return
  fi
  verdict_of "$label" "$OUT/mutant.bin" "$FWMAIN" "$HELPER"
}

# ctl_src <label> <sed-script> — mutate `fw_main.cpp` and run the CLEAN binary against the mutated COPY. The
# structural pin reads the path it is handed, so no rebuild is needed and none is pretended.
ctl_src() {
  local label=$1 script=$2
  sed "$script" "$FWMAIN" > "$OUT/mutant_fw_main.cpp"
  if cmp -s "$FWMAIN" "$OUT/mutant_fw_main.cpp"; then
    n_bad=$((n_bad+1)); printf '  FAIL %s — the mutation changed NOTHING (VACUOUS)\n' "$label"; return
  fi
  verdict_of "$label" "$OUT/probe" "$OUT/mutant_fw_main.cpp" "$HELPER"
}

if [ "${1:-}" != "--no-neg" ]; then
  echo
  echo "== negative controls (each MUST turn the probe RED) =="

  # ⛔ THE CONTROL OF THE CONTROLS ([[B237]]): a binary that REALLY dies must classify as `abnormal`, and the
  #    classifier must still separate a genuine reddening from a crash and from a silent non-zero exit.
  printf 'int main() { volatile int* p = 0; return *p; }\n' > "$OUT/crasher.cpp"
  if "$CXX" -O0 "$OUT/crasher.cpp" -o "$OUT/crasher" 2>/dev/null; then
    b237_rc=0
    bash -c '"$1"; exit $?' _ "$OUT/crasher" > "$OUT/crash.out" 2>&1 || b237_rc=$?
    b237_fails=$(grep -c '^  FAIL ' "$OUT/crash.out")
    if [ "$b237_rc" -ge 128 ] && [ "$(classify_control "$b237_rc" "$b237_fails")" = abnormal ] \
       && [ "$(classify_control 1 3)" = red ] && [ "$(classify_control 0 0)" = passes ] \
       && [ "$(classify_control 1 0)" = silent ]; then
      echo "  ok   a real SIGSEGV classifies as UNUSABLE, and 1/3 red · 0/0 passes · 1/0 silent still discriminate"
      n_ctl=$((n_ctl+1))
    else
      echo "  FAIL the control classifier does not hold"; n_bad=$((n_bad+1))
    fi
  else
    echo "  FAIL the crash repro did not build — the [[B237]] rule is unmeasured"; n_bad=$((n_bad+1))
  fi

  # ---- C1: THE SWITCH STOPS DELEGATING. The operator line disappears from the firmware entirely, and no native
  #          case, no simulator stream and no board build can see it — the whole reason this probe exists.
  ctl_src 'C1  the `fw_main.cpp` switch arm no longer calls the renderer' \
      '/mrfw::print_custody_failure(mrcon, pu);/d'

  # ---- C2: THE DIRECT LINE CHANGES. §B278 S4 must be byte-compatible for a direct report; a renamed field is
  #          exactly the silent app-boundary break the golden exists to catch.
  ctl_hdr 'C2  the DIRECT line renames `ctr=` to `ctrh=`' \
      's|out.print(F(" ctr="));    out.print(cf->failed_ctr);|out.print(F(" ctrh="));   out.print(cf->failed_ctr);|'

  # ---- C3: THE TRANSLATED BRANCH IS BYPASSED. Every translated record renders as an ordinary direct one, which
  #          is the "a success that isn't" shape: a true-looking line describing the wrong fact.
  ctl_hdr 'C3  the translated block is never entered (a translated record renders as a direct one)' \
      's|if (meshroute::custody_record_is_translated(cf->notice_flags)) {|if (false) {|'

  # ---- C4: `mobile_ctr` CARRIES ctrH. The mobile is told to correlate on a counter it never used, and the two
  #          numbers are plausible neighbours — the defect a "looks like a counter" eyeball check passes.
  ctl_hdr 'C4  `mobile_ctr` prints the HOME counter instead of ctrM' \
      's|out.print(F(" mobile_ctr=")); out.print(t->mobile_ctr);|out.print(F(" mobile_ctr=")); out.print(cf->failed_ctr);|'

  # ---- C5: THE TARGET KIND IS HARD-WIRED. A hash-addressed report claims a node-id target — a display-shaped
  #          field asserting an identity the record does not carry.
  ctl_hdr 'C5  the target KIND word is hard-wired to "node_id"' \
      's|out.print(custody_target_word(t->target_kind));|out.print("node_id");|'

  # ---- C6: BOTH TARGET FIELDS ARE PRINTED. §8.4 forbids it explicitly: a duplicated value is a second
  #          compatibility surface, and one of the two is always meaningless.
  ctl_hdr 'C6  BOTH `target_id` and `target_hash` are printed' \
      's|            } else {\n|XXX|; s|if (t->target_kind == meshroute::CustodyTranslatedTargetKind::key_hash) {|if (true) { out.print(F(" target_id=")); out.print(static_cast<unsigned long>(t->target_value)); }\n            if (t->target_kind == meshroute::CustodyTranslatedTargetKind::key_hash) {|'

  # ---- C7: AN ALIAS IS INTRODUCED. `via_home` is the exact alias §8.4 names and refuses.
  ctl_hdr 'C7  a `via_home` alias is added beside the target' \
      's|            out.print(F(" mobile_ctr=")); out.print(t->mobile_ctr);|            out.print(F(" via_home=")); out.print(cf->failed_origin);\n            out.print(F(" mobile_ctr=")); out.print(t->mobile_ctr);|'

  # ---- C8: THE WARNING IS REMOVED. §14.3, verbatim: *"No output may call it a NACK or claim non-delivery"* —
  #          and a custody report without its caveat reads as exactly that claim.
  ctl_hdr 'C8  the `NOT proof the destination missed it` warning is dropped' \
      's|    out.println(F(" — the relay could not complete onward custody; NOT proof the destination missed it (an e2e ack may still arrive)"));|    out.println(F(""));|'

  # ---- C9: AN UNPARSEABLE RECORD IS SWALLOWED. The loud line becomes a silent drop — the C2 fail-loud rule
  #          inverted, and the one shape where the operator is told nothing at all.
  ctl_hdr 'C9  an unparseable record is dropped SILENTLY instead of loudly' \
      's|    if (!cf) { out.println(F("CUSTODY FAILURE (unparseable record)")); return; }|    if (!cf) { return; }|'
fi

MD5_AFTER=$(md5_sources)
echo
if [ "$MD5_BEFORE" != "$MD5_AFTER" ]; then
  echo "FAIL — the probe MODIFIED a real source file (md5 $MD5_BEFORE -> $MD5_AFTER)"; rc=1
else
  echo "tree unchanged: the real sources' md5 is identical before and after ($MD5_BEFORE)"
fi

# ================================================================================================================
# ★★★ THE VERDICT — EVERY TERM ENFORCED, NOT MERELY PRINTED.
# ================================================================================================================
echo "probe: $n_checks checks against the REAL print_custody_failure(), $n_probe_fail failed (pin $PIN_CHECKS)"
[ "$probe_rc" -eq 0 ]            || { echo "  !! the clean probe EXITED $probe_rc"; rc=1; }
[ "$n_probe_fail" -eq 0 ]        || { echo "  !! the clean probe reported $n_probe_fail failed check(s)"; rc=1; }
[ "$n_checks" -eq "$PIN_CHECKS" ] || {
  echo "  !! CHECK COUNT MOVED: $n_checks, pinned $PIN_CHECKS — a check was added or deleted."
  echo "     If deliberate, update PIN_CHECKS at the top of this file WITH its derivation."; rc=1; }

if [ "${1:-}" != "--no-neg" ]; then
  echo "controls: $n_ctl verified / $n_bad unusable (pin $PIN_CONTROLS)"
  [ "$n_bad" -eq 0 ]               || rc=1
  [ "$n_ctl" -eq "$PIN_CONTROLS" ] || {
    echo "  !! CONTROL COUNT MOVED: $n_ctl, pinned $PIN_CONTROLS — a negative control was added or deleted."
    echo "     If deliberate, update PIN_CONTROLS at the top of this file."; rc=1; }
  [ "$rc" -eq 0 ] && echo "PASS" || echo "FAIL"
else
  # ⛔ `--no-neg` MUST NEVER PRINT AN ORDINARY `PASS`. Without the controls this run cannot say the checks CAN
  #    fail, which is the sibling probe's documented trap.
  if [ "$rc" -eq 0 ]; then echo "PROBE-ONLY — NOT A GATE (controls skipped; run without --no-neg to gate)"
  else                     echo "FAIL"; fi
fi
exit $rc
