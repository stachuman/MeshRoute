#!/usr/bin/env bash
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# §CUSTODY-D FEATURE PROBE — the inbox console verbs' PRODUCTION WIRING, host-compiled and host-RUN.
#
# WHY THIS EXISTS. `platformio.ini`'s native env sets `test_build_src = no`, so neither the doctest suite nor the
# simulator compiles ANY `src/*.cpp`. §CUSTODY-D added a DESTRUCTIVE console command (`clear_inbox confirm`) whose
# every gate sat BELOW that seam — the token predicate, `Inbox::clear()` and the ack writers are all pure units.
# ★ QG's finding: all of those gates stay GREEN if the dispatch arm is deleted, if the confirmation check is
#   bypassed, if the refusal's early `return` is removed, or if a constant `true` is passed to
#   `inbox_clear_result()`. This probe links the REAL `src/firmware_commands.cpp` + `src/firmware_inbox.cpp`
#   against the real `lib/core`/`lib/console` and DRIVES `mrfw::dispatch()` — the one router both transports use.
#   The four controls below are exactly those four defects, each required to turn the probe RED.
#
# ★★ IT MUST REMAIN IN THE REPOSITORY AND BE COMMITTED WITH THIS SLICE, for the reason the sibling probes state at
#    this spot: this project has already LOST a proven 33-assert scenario to a session scratchpad
#    ([[meshroute-agent-scratchpad-is-volatile]]). A recipe in a note is not a storage location.
#    ⓘ Verify tracked-ness with `git ls-files`, never from this comment (the sibling probes' standing lesson: a
#      document asserting a state that has since changed).
#
# THE ARM. One arm, `[env:heltec_v3]`'s: `-DARDUINO=100 -DMR_CONSOLE=1 -DBOARD_HELTEC_V3`. ⛔ It must mirror a REAL
# env or the probe measures a configuration no board builds. `-DARDUINO` selects the real staged `console_sink.h`;
# `BOARD_HELTEC_V3` selects the ESP32 NV/inbox arm, which is what this probe's own `fakes/` stand in for.
#
# FAKES — REUSED, NOT FORKED (U1):
#   · `tools/probe_board_ui/fakes/`     — Arduino.h (Print/Serial/millis/F). ⓘ §CUSTODY-D ADDED `Print`'s numeric
#     overloads + `Serial::print/println` + the DEC/HEX/OCT/BIN constants there, ADDITIVELY: real Arduino `Print`
#     has them, and without them `firmware_commands.cpp` (which prints integers by the hundred) cannot host-compile
#     at all. Both sibling probes were re-run at their published counts after that edit.
#   · `tools/probe_device_radio/fakes/` — RadioLib.h + CustomSX1262.h. ⓘ §CUSTODY-D added an empty `Module` class
#     there, additively: `fw_context.h:44` declares `extern Module g_mod`, so the TYPE must exist for the
#     declaration to parse. No probe calls a `Module` method.
#   · `tools/probe_inbox_verbs/fakes/`  — this arm's ESP32 platform headers only (Preferences/nvs/LittleFS/
#     esp_random/bootloader_random). They belong to nobody else, so they are NOT pushed into a shared dir.
#
# USAGE:  tools/probe_inbox_verbs/run.sh            # probe + NEGATIVE CONTROLS (the controls run BY DEFAULT)
#         tools/probe_inbox_verbs/run.sh --no-neg   # probe only — NOT a gate, use only while iterating
# ⚠ The controls run by default DELIBERATELY (the sibling probe's trap: controls documented as "not optional" while
#   the standard command skipped them, so the reported gate never included them).
#
# ★★★ WHAT A CONTROL HAS TO BE. Each applies ONE mutation to a COPY of a REAL production source — the tempting
#     WRONG SHAPE, not merely a deletion — and must make the probe RED. Four ways a control can be worthless, all
#     four checked: (1) the sed matched nothing -> VACUOUS; (2) the mutant does not compile -> the probe never ran;
#     (3) the probe still passes -> the check measures nothing; (4) ⛔ [[B237]] the mutant DIED (a signal, or a
#     non-zero exit with ZERO `  FAIL ` lines) -> UNUSABLE, never "verified" — see `classify_control`.
# ⚠ And the tree must come out untouched: the real sources' md5 is captured before and asserted after.

set -uo pipefail
cd "$(dirname "$0")" || exit 1
ROOT=$(cd ../.. && pwd)          # ★ absolute — a relative path in a cwd-resetting shell silently measured nothing
HERE=$(pwd)                      #   once already ([[B82]]). Never make these relative.
CXX=${CXX:-g++}
CC=${CC:-gcc}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

FW_CMDS="$ROOT/src/firmware_commands.cpp"   # THE ROUTER   — controls C1/C2 mutate this
FW_INBOX="$ROOT/src/firmware_inbox.cpp"     # THE HANDLER  — controls C3/C4 mutate this
FW_ACK="$ROOT/lib/console/console_json.h"   # the verdict -> lexeme mapping — control C5 mutates this

DEFS=(-DARDUINO=100 -DMR_CONSOLE=1 -DBOARD_HELTEC_V3)
INCS=(-I"$HERE/fakes" -I"$ROOT/tools/probe_board_ui/fakes" -I"$ROOT/tools/probe_device_radio/fakes"
      -I"$ROOT/variants/heltec_common" -I"$ROOT/src" -I"$ROOT/lib/hal" -I"$ROOT/lib/core" -I"$ROOT/lib/console"
      -I"$ROOT/lib/monocypher/src")
STD=(-std=gnu++20 -fno-exceptions -fno-rtti -O0)

# ================================================================================================================
# ★★★ THE TWO PINNED COUNTS — the `PIN_CASES` idiom from `tools/probe_ui_model_mutations.py`, applied here for the
#     same reason it exists there: WITHOUT A PIN, DELETING A CHECK IS INVISIBLE. A probe that reports "0 failed"
#     over 3 surviving checks is indistinguishable, in its exit code and in its summary line, from one that ran all
#     39 — so coverage can shrink to nothing while the gate stays green. Growing or trimming the probe therefore
#     means DELIBERATELY updating a number here, with the derivation.
#
# PIN_CHECKS = 39, derived by running the clean probe and counting its `  ok  `/`  FAIL ` lines:
#     W1 route owned 1 · W1b routed nowhere else 1
#     W2 exact needs_confirm bytes 1 · W2b neither store wiped 1 · W2c clear not entered 1 · W2d records intact 1
#     W3 the hardened token corpus, one check per line 7
#     W4 confirm owned 1 · W4b exact cleared ack 1 · W4c both wiped once 1 · W4d both empty 1
#        · W4e high-water persisted first 1 · W4f cursors reset 1 · W4g no foreign handler 1
#        · W4h production stores untouched 1
#     W5 exact io_error ack 1 · W5b `cleared` absent 1 · W5c no short-circuit 1 · W5d healthy store erased 1
#     W6 channel-side failure 1 · W6b the two halves 1
#     W7 high-water refusal 1 · W7b neither erased 1 · W7c records survive 1
#     W8 boundary 1 · W8b no ack/no clear 1 · W8c short verb 1 · W8d bare `clear` 1
#     W9 second (BLE-shaped) sink 1 · W9b inert there too 1
#     W10 del_msg 1 · W10b mark_read 1 · W10c pull_inbox 1
#     2+4+7+8+4+2+3+4+2+3 = 39. ✓
# PIN_CONTROLS = 8: the [[B237]] control-of-the-controls + C1..C7.
PIN_CHECKS=39
PIN_CONTROLS=8

# ---- the tree must not move -------------------------------------------------------------------------------------
# ⛔ SPELLED ONCE, IN A FUNCTION, AND THAT IS A FIX RATHER THAN TIDINESS: the sibling probe once had two `cat` lists
#    1470 lines apart, a slice added a file to one of them, and the tripwire reported "the probe MODIFIED a real
#    source" on a tree nothing had touched. A FALSE RED is as bad as a false green.
md5_sources() {
  cat "$FW_CMDS" "$FW_INBOX" "$FW_ACK" "$HERE/probe_main.cpp" \
      "$ROOT/tools/probe_board_ui/fakes/Arduino.h" "$ROOT/tools/probe_device_radio/fakes/RadioLib.h" \
      "$ROOT/src/firmware_config_parse.h" "$ROOT/lib/core/inbox.h" | md5sum | cut -d' ' -f1
}
MD5_BEFORE=$(md5_sources)

echo "== §CUSTODY-D inbox-verb wiring probe (real dispatch() + real handle_clear_inbox, host-linked) =="

# ---- the support library: the REAL lib/core + lib/console, compiled ONCE ------------------------------------------
# ⓘ No -Werror on this half: `lib/core/node_hashlocate.cpp` carries a PRE-EXISTING -Wmisleading-indentation and
#   `device_radio.h` a -Wvolatile, neither of which this slice owns. The probe's OWN TU is built -Werror below.
build_support() {
  local s o
  for s in "$ROOT"/lib/core/*.cpp "$ROOT/lib/hal/device_hal.cpp" "$ROOT/lib/hal/timer_wheel.cpp" \
           "$ROOT/lib/hal/airtime_ledger.cpp" "$ROOT/lib/console/console_json.cpp" \
           "$ROOT/lib/console/console_parse.cpp"; do
    o="$OUT/sup_$(basename "$s" .cpp).o"
    "$CXX" "${STD[@]}" -Wall -Wextra "${DEFS[@]}" "${INCS[@]}" -c "$s" -o "$o" 2>/dev/null || {
      echo "SUPPORT BUILD FAILED: $s"; return 1; }
  done
  "$CC" -std=gnu17 -O0 -I"$ROOT/lib/monocypher/src" -c "$ROOT/lib/monocypher/src/monocypher.c" \
       -o "$OUT/sup_monocypher.o" 2>/dev/null || return 1
  # The probe's own TU is held to -Werror: it is this slice's code.
  # ⛔ TWO SUPPRESSIONS, AND EACH IS NAMED RATHER THAN BLANKET — they are PRE-EXISTING diagnostics from HEADERS the
  #    probe merely includes, not from `probe_main.cpp`, and neither is §CUSTODY-D's to fix:
  #      · `-Wno-volatile`  — `lib/hal/device_radio.h:342` does `++g_rxbad_count` on a `volatile` counter. This is
  #        the very diagnostic `src/fw_context_pure.h`'s header note names as §B106's cost of including
  #        `fw_context.h`; it is pinned in the board warning census and is not a probe defect.
  #      · `-Wno-deprecated-declarations` — `src/device_inbox_fs_esp32.h:179` calls `readdir_r`, which glibc
  #        deprecates and the ESP-IDF newlib does not. The board builds never see it; only a host build does.
  #    ⓘ Both are compiled-in HEADERS, so they cannot be scoped away by moving code. Everything `probe_main.cpp`
  #      itself writes is still -Wall -Wextra -Werror clean.
  "$CXX" "${STD[@]}" -Wall -Wextra -Werror -Wno-volatile -Wno-deprecated-declarations \
       "${DEFS[@]}" "${INCS[@]}" -c "$HERE/probe_main.cpp" \
       -o "$OUT/probe_main.o" 2>"$OUT/pm.log" || { echo "PROBE MAIN BUILD FAILED:"; head -20 "$OUT/pm.log"; return 1; }
  return 0
}

# build_variant <router.cpp> <handler.cpp> <ack.h-dir-or-empty> <out-binary>
# ⛔ The two production TUs are passed as PATHS so a control can hand in a MUTATED COPY of either without the probe
#   ever writing to the repository. A mutated `console_json.h` is handed in as an include DIR that shadows the real
#   one — same principle, applied to a header.
build_variant() {
  local router=$1 handler=$2 ackdir=$3 bin=$4
  local pre=()
  [ -n "$ackdir" ] && pre=(-I"$ackdir")
  : > "$OUT/build.log"
  "$CXX" "${STD[@]}" -Wall -Wextra "${pre[@]}" "${DEFS[@]}" "${INCS[@]}" -c "$router" -o "$OUT/v_cmds.o" 2>>"$OUT/build.log" \
    && "$CXX" "${STD[@]}" -Wall -Wextra "${pre[@]}" "${DEFS[@]}" "${INCS[@]}" -c "$handler" -o "$OUT/v_inbox.o" 2>>"$OUT/build.log" \
    && "$CXX" "${STD[@]}" -Wall -Wextra "${pre[@]}" "${DEFS[@]}" "${INCS[@]}" -c "$HERE/probe_main.cpp" -o "$OUT/v_main.o" 2>>"$OUT/build.log" \
    && "$CXX" "$OUT/v_main.o" "$OUT/v_cmds.o" "$OUT/v_inbox.o" "$OUT"/sup_*.o -o "$bin" 2>>"$OUT/build.log"
}

rc=0
if ! build_support; then echo "FAIL — the support half did not build"; exit 1; fi
if ! build_variant "$FW_CMDS" "$FW_INBOX" "" "$OUT/probe"; then
  echo "PROBE BUILD FAILED — the real src/ TUs did not host-compile:"
  sed 's/^/    /' "$OUT/build.log" | head -25
  exit 1
fi
# ⛔ TEE'd, not just run: the summary line at the bottom reports how many checks actually EXECUTED, and a count
#    taken from a file the probe never wrote would report 0 on a perfectly good run — an instrument reporting
#    "measured nothing" about itself is the one number a reader must be able to trust ([[B227]]/[[B237]]).
# ⛔⛔ THE PIPELINE RUNS BARE AND `PIPESTATUS` IS READ ON THE VERY NEXT LINE. It used to end `|| true`, and that was
#     the SAME self-honesty defect as the 0-checks one, one line over (QG, 2026-08-31): when the probe FAILS, bash
#     runs `true`, and `true` is a simple command that REPLACES `PIPESTATUS` with `(0)` — so the next line read a
#     failing probe as a passing one. Reproduced directly:
#         set -uo pipefail; false | tee /dev/null || true; echo ${PIPESTATUS[0]}   ->  0
#         set -uo pipefail; false | tee /dev/null;         echo ${PIPESTATUS[0]}   ->  1
#     ⓘ `set -e` is deliberately NOT on in this file, so a bare failing pipeline does not exit here — the verdict
#       block below owns the decision, which is the only place it should be owned.
"$OUT/probe" 2>&1 | tee "$OUT/probe.out"
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

# ctl <label> <which:router|handler|ack> <sed-script>
ctl() {
  local label=$1 which=$2 script=$3
  local router="$FW_CMDS" handler="$FW_INBOX" ackdir=""
  case "$which" in
    router)  sed "$script" "$FW_CMDS"  > "$OUT/mutant_cmds.cpp";  router="$OUT/mutant_cmds.cpp"
             cmp -s "$FW_CMDS" "$router"  && { n_bad=$((n_bad+1)); printf '  FAIL %s — the mutation changed NOTHING (VACUOUS)\n' "$label"; return; } ;;
    handler) sed "$script" "$FW_INBOX" > "$OUT/mutant_inbox.cpp"; handler="$OUT/mutant_inbox.cpp"
             cmp -s "$FW_INBOX" "$handler" && { n_bad=$((n_bad+1)); printf '  FAIL %s — the mutation changed NOTHING (VACUOUS)\n' "$label"; return; } ;;
    ack)     mkdir -p "$OUT/ackinc"; sed "$script" "$FW_ACK" > "$OUT/ackinc/console_json.h"; ackdir="$OUT/ackinc"
             cmp -s "$FW_ACK" "$OUT/ackinc/console_json.h" && { n_bad=$((n_bad+1)); printf '  FAIL %s — the mutation changed NOTHING (VACUOUS)\n' "$label"; return; } ;;
  esac
  if ! build_variant "$router" "$handler" "$ackdir" "$OUT/mutant.bin"; then
    n_bad=$((n_bad+1))
    printf '  FAIL %s — the mutant does not COMPILE, so the probe never ran against it:\n' "$label"
    sed 's/^/        /' "$OUT/build.log" | head -6; return
  fi
  # ⛔⛔ [[B237]] — the verdict is `classify_control`'s, NEVER a bare "did it exit non-zero". The inner `bash -c`
  #     with a trailing `exit $?` keeps a crash notice OUT of this gate's console (bash EXECs a lone final command).
  local rc_m=0
  bash -c '"$1"; exit $?' _ "$OUT/mutant.bin" >"$OUT/mutant.out" 2>&1 || rc_m=$?
  local fails; fails=$(grep -c '^  FAIL ' "$OUT/mutant.out")
  local verdict; verdict=$(classify_control "$rc_m" "$fails")
  case "$verdict" in
    passes)   n_bad=$((n_bad+1)); printf '  FAIL %s — the probe still PASSES against the mutant (measures nothing)\n' "$label" ;;
    abnormal) n_bad=$((n_bad+1)); printf '  FAIL %s — the mutant DIED (exit %s, %s failure(s)); a crash measures nothing\n' "$label" "$rc_m" "$fails" ;;
    silent)   n_bad=$((n_bad+1)); printf '  FAIL %s — non-zero exit with ZERO named failures; nothing to attribute\n' "$label" ;;
    red)      n_ctl=$((n_ctl+1)); printf '  ok   %s -> RED (%s check(s) failed)\n' "$label" "$fails" ;;
  esac
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

  # ---- C1: THE DISPATCH ARM REMOVED. The verb becomes unreachable; the router answers its unknown-verb path and
  #          every ack in the family becomes unreachable with it. This is QG's first named defect.
  ctl 'C1  the `clear_inbox` DISPATCH ARM is deleted (the verb is unreachable)' router \
      '/!strncmp(line, "clear_inbox", 11)/d'

  # ---- C2: THE ARM'S BOUNDARY CONSTANT IS WRONG. A subtler shape than deletion and one a structural grep would
  #          likely still match: the arm exists, names the right verb, and routes the WRONG span of the line.
  ctl 'C2  the dispatch arm off-by-one (`line[11]` -> `line[10]`, `+ 11` -> `+ 10`)' router \
      's|(len == 11 \|\| (len > 11 \&\& line\[11\] == . .)) \&\& !strncmp(line, "clear_inbox", 11)) { handle_clear_inbox(line + 11, len - 11|(len == 11 \|\| (len > 11 \&\& line[10] == '"'"' '"'"')) \&\& !strncmp(line, "clear_inbox", 11)) { handle_clear_inbox(line + 10, len - 10|'

  # ---- C3: THE CONFIRMATION CHECK BYPASSED. A bare `clear_inbox` destroys the inbox. QG's second named defect.
  ctl 'C3  the confirmation check is BYPASSED (`!parse_confirm_token(...)` -> `false`)' handler \
      's/if (!parse_confirm_token(args, n)) {/if (false) {/'

  # ---- C4: THE REFUSAL'S EARLY RETURN REMOVED. It prints `needs_confirm` AND clears — the worst of both, and the
  #          one shape where the operator is told nothing happened while everything did. QG's third named defect.
  ctl 'C4  the refusal early `return` is REMOVED (it refuses AND clears)' handler \
      's|        return;                                           // ⛔ NO state is read and nothing is touched||'

  # ---- C5: THE VERDICT FORCED SUCCESSFUL. A failed clear prints `cleared`. QG's fourth named defect, and the
  #          [[B134]] data-retention lie in this slice's shape.
  ctl 'C5  the verdict is forced TRUE into `inbox_clear_result()` (failure prints `cleared`)' ack \
      's/inline const char\* inbox_clear_result(bool cleared) { return cleared ? "cleared" : "io_error"; }/inline const char* inbox_clear_result(bool) { return "cleared"; }/'

  # ---- C6: THE CLEAR IS NEVER PERFORMED but the ack still claims it. The mirror image of C5: an ack that reports
  #          a destruction the handler declined to do. Neither pure units nor a structural grep can see this.
  ctl 'C6  `ib.clear()` is replaced by an unconditional success (the ack claims a clear that never ran)' handler \
      's/const bool cleared = ib.clear();/const bool cleared = true;/'

  # ---- C7: THE ARM ROUTES TO THE WRONG HANDLER — the "insert beside its neighbours" property, attacked directly.
  ctl 'C7  the dispatch arm routes `clear_inbox` to `handle_del_msg` instead' router \
      's/{ handle_clear_inbox(line + 11, len - 11, out); return true; }/{ handle_del_msg(line + 11, out); return true; }/'
fi

MD5_AFTER=$(md5_sources)
echo
if [ "$MD5_BEFORE" != "$MD5_AFTER" ]; then
  echo "FAIL — the probe MODIFIED a real source file (md5 $MD5_BEFORE -> $MD5_AFTER)"; rc=1
else
  echo "tree unchanged: the real sources' md5 is identical before and after ($MD5_BEFORE)"
fi

# ================================================================================================================
# ★★★ THE VERDICT — EVERY TERM ENFORCED, NOT MERELY PRINTED (QG, 2026-08-31).
#     The previous form printed `n_probe_fail` and never read it, and pinned neither count, so the runner could
#     report PASS while its own clean probe failed or while checks/controls had been silently deleted. Each line
#     below therefore NAMES the term it is failing on, so a red gate says WHICH property broke.
# ================================================================================================================
echo "probe: $n_checks checks against the REAL dispatch()+handle_clear_inbox, $n_probe_fail failed (pin $PIN_CHECKS)"
[ "$probe_rc" -eq 0 ]            || { echo "  !! the clean probe EXITED $probe_rc"; rc=1; }
[ "$n_probe_fail" -eq 0 ]        || { echo "  !! the clean probe reported $n_probe_fail failed check(s)"; rc=1; }
[ "$n_checks" -eq "$PIN_CHECKS" ] || {
  echo "  !! CHECK COUNT MOVED: $n_checks, pinned $PIN_CHECKS — a check was added or deleted."
  echo "     If deliberate, update PIN_CHECKS at the top of this file WITH its derivation."; rc=1; }

if [ "${1:-}" != "--no-neg" ]; then
  echo "controls: $n_ctl verified / $n_bad unusable (pin $PIN_CONTROLS)"
  [ "$n_bad" -eq 0 ]                    || rc=1
  [ "$n_ctl" -eq "$PIN_CONTROLS" ]      || {
    echo "  !! CONTROL COUNT MOVED: $n_ctl, pinned $PIN_CONTROLS — a negative control was added or deleted."
    echo "     If deliberate, update PIN_CONTROLS at the top of this file."; rc=1; }
  [ "$rc" -eq 0 ] && echo "PASS" || echo "FAIL"
else
  # ⛔ `--no-neg` MUST NEVER PRINT AN ORDINARY `PASS`. Without the controls this run cannot say the checks CAN
  #    fail, which is the sibling probe's documented trap: controls described as "not optional" while the standard
  #    command skipped them, so the reported gate never included them. A distinct verdict word is the fix.
  if [ "$rc" -eq 0 ]; then echo "PROBE-ONLY — NOT A GATE (controls skipped; run without --no-neg to gate)"
  else                     echo "FAIL"; fi
fi
exit $rc
