#!/usr/bin/env bash
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# §0f BLE INBOUND-LINE PROBE — the REAL `src/device_ble.h` intake, host-compiled and host-RUN.
#
# WHY THIS EXISTS. `platformio.ini`'s native env sets `test_build_src = no`, and the simulator compiles `lib/core`
# only, so NEITHER automated gate compiles a single `src/` translation unit. `service_rx()` /
# `dispatch_current_line()` — the byte machine that decides WHICH COMMAND LINES EXIST over BLE — therefore had no
# automated cover at all. Slice 0f moves that machine's admission edge from a 160-byte literal to a capacity DERIVED
# from the console grammar, so both halves have to be measured: the derivation (structurally, on the header text) and
# the edge (behaviourally, by feeding the real intake real ATT-sized chunks).
#
# ★★ IT MUST REMAIN IN THE REPOSITORY AND BE COMMITTED WITH THIS SLICE, for the reason its sibling probes state at
#    this spot: this project has already LOST a proven 33-assert scenario to a session scratchpad
#    ([[meshroute-agent-scratchpad-is-volatile]]). A recipe in a note is not a storage location.
#    ⓘ Verify tracked-ness with `git ls-files`, never from this comment.
#
# FAKES — REUSED, NOT FORKED (U1): the Arduino surface comes from `tools/probe_console_sink/fakes/Arduino.h` and
#   nowhere else. This probe adds only what NO existing fake provides: a Bluefruit/NUS shim and two Nordic
#   register/SVCALL stubs that `device_rng.h` NAMES but that no code path here CALLS. ⛔ None of them holds a line
#   buffer, a newline rule or a length limit — all of that is the real header's, which is the only reason this is a
#   gate rather than a model of itself.
#
# WHAT IS BEHAVIOURAL AND WHAT IS STRUCTURAL, said plainly rather than dressed up:
#   • BEHAVIOURAL — the executed intake (chunking invariance at 1/20/244 B, the admitted edge, the refused edge,
#     CR handling, empty lines, the reply path, overflow recovery), and the EXECUTED cross-pin of the transitional
#     226-byte cross-layer body-cap mirror against the real `pack_unicast_inner` at cap and cap+1.
#   • STRUCTURAL — that the capacity is DERIVED (every term named or `sizeof("literal") - 1`), that exactly one
#     capacity expression controls admission, and that the refusal literal is unchanged. Necessary, NOT sufficient;
#     the behavioural half above is what makes it a gate.
#   • NOT MEASURED HERE — what the COMMAND does with an admitted line (`err_unsupported`, `too_large`, `queued`).
#     Those belong to `Node::on_command` and are owned by the native tests. This probe proves only that the BYTES
#     arrive intact, which is the precondition those named refusals depend on.
#
# USAGE:  tools/probe_ble_line/run.sh            # probe + NEGATIVE CONTROLS (the controls run BY DEFAULT)
#         tools/probe_ble_line/run.sh --no-neg   # probe only — NOT a gate, use only while iterating
# ⚠ The controls run by default DELIBERATELY (the sibling probes' documented trap: controls described as "not
#   optional" while the standard command skipped them, so the reported gate never included them).
#
# ★★★ WHAT A CONTROL HAS TO BE. Each applies ONE mutation to a COPY of the real header — the tempting WRONG SHAPE,
#     not merely a deletion — and must make the probe RED. Four ways a control can be worthless, all four checked:
#     (1) the sed matched nothing -> VACUOUS; (2) the mutant does not compile -> the probe never ran; (3) the probe
#     still passes -> the check measures nothing; (4) ⛔ [[B237]] the mutant DIED (a signal, or a non-zero exit with
#     ZERO `  FAIL ` lines) -> UNUSABLE, never "verified" — see `classify_control`.
# ⚠ And the tree must come out untouched: the real sources' md5 is captured before and asserted after.

set -uo pipefail
cd "$(dirname "$0")" || exit 1
ROOT=$(cd ../.. && pwd)          # ★ absolute — a relative path in a cwd-resetting shell silently measured nothing
HERE=$(pwd)                      #   once already ([[B82]]). Never make these relative.
CXX=${CXX:-g++}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

HDR="$ROOT/src/device_ble.h"                  # THE FILE UNDER TEST — every control mutates a COPY of it
FAKES="$ROOT/tools/probe_console_sink/fakes"  # the ONE Arduino fake (U1: reused, never forked)

# ⚠ These -D MUST mirror what a real nRF52 build hands `device_ble.h`, or the probe measures a configuration no
#   board builds — the vacuous-instrument failure the controls exist to catch. `MRBLE_NRF52` is derived, not forced:
#   ARDUINO + NRF52_SERIES are exactly the macros `platformio.ini`'s nRF52 envs define, and the header's own `#if`
#   is what turns the device arm on.
STD=(-std=gnu++2a -fno-exceptions -fno-rtti -O0)
DEFS=(-DARDUINO=100 -DNRF52_SERIES -DMR_CONSOLE=1)
INCS=(-I"$HERE/fakes" -I"$FAKES" -I"$ROOT/src" -I"$ROOT/lib/core" -I"$ROOT/lib/hal")
# ⓘ `-Wno-volatile` is DECLARED, not hidden: `device_ble.h`'s connect/disconnect callbacks increment a
#   `volatile uint8_t` (a deliberate, documented nRF52 idiom — see the comment at its declaration), which C++20
#   deprecates. It is PRE-EXISTING and this slice does not own it; everything else is -Werror.
WARN=(-Wall -Wextra -Werror -Wno-volatile)

# ================================================================================================================
# ★★★ THE TWO PINNED COUNTS — the `PIN_CASES` idiom from `tools/probe_ui_model_mutations.py`, applied here for the
#     same reason it exists there: WITHOUT A PIN, DELETING A CHECK IS INVISIBLE. A probe that reports "0 failed"
#     over 3 surviving checks is indistinguishable, in its exit code and its summary line, from one that ran all 40.
#
# PIN_CHECKS = 40, derived by running the clean probe and counting its `  ok  `/`  FAIL ` lines:
#     A1..A11  source-integrity pin + the structural derivation family                       11
#     B1..B3   the executed pack_unicast_inner cross-pin (cap · cap+1 · depth-is-binding)      3
#     C1..C4   begin() + the three canonical spellings measured against the derived terms      4
#     C5..C8   the binding transport-positive under 1/20/244-B writes + their mutual identity  4
#     C9..C13  single write · uneven final chunk · boundary before newline · queue-positive
#              · the maximal `send` line                                                       5
#     C14..C17 CR handling · empty LF · empty CRLF · a one-byte line                           4
#     C18..C19 the reply path (non-empty written unchanged · zero-length writes nothing)       2
#     C20..C26 the refusal edge ×3 chunkings · the admitted byte below it · refuse-once
#              · never-split-into-a-second-command · overflow recovery                         7
#     11+3+4+4+5+4+2+7 = 40. ✓
# PIN_CONTROLS = 8: the [[B237]] control-of-the-controls + N1..N7.
# 8ac: +8 direct-output byte identity and ATT bounds (2 lengths x 2 MTUs x 2 checks).
# Seven checked entropy provider/failure checks.
PIN_CHECKS=55
PIN_CONTROLS=12

# ---- the tree must not move ------------------------------------------------------------------------------------
# ⛔ SPELLED ONCE, IN A FUNCTION (the sibling probe's lesson: two `cat` lists drifted apart and produced a FALSE RED
#    on a tree nothing had touched).
md5_sources() {
  cat "$HDR" "$ROOT/src/device_rng.h" "$HERE/probe_main.cpp" "$HERE/fakes/bluefruit.h" "$HERE/fakes/nrf.h" "$HERE/fakes/nrf_soc.h" \
      "$FAKES/Arduino.h" "$ROOT/lib/core/frame_codec.cpp" | md5sum | cut -d' ' -f1
}
MD5_BEFORE=$(md5_sources)

# FNV-1a-64 of a file — the runner's half of the source-integrity pin. The probe recomputes it in C++ over the file
# it is handed, so the two implementations have to agree; that is what forbids "compile a copy, inspect the tree".
fnv() { python3 -c '
import sys
h = 14695981039346656037
for b in open(sys.argv[1], "rb").read():
    h ^= b; h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
print("0x%016xULL" % h)' "$1"; }

echo "== §0f BLE inbound-line probe (the REAL src/device_ble.h intake + the real pack_unicast_inner) =="
echo "   header under test : $HDR"
echo "   md5(header)       : $(md5sum "$HDR" | cut -d' ' -f1)"

# ---- the support half: the REAL lib/core packer, compiled ONCE --------------------------------------------------
# ⓘ No -Werror here: `lib/core` carries pre-existing diagnostics this slice does not own. The probe's OWN TU is
#   built -Werror above.
if ! "$CXX" "${STD[@]}" -Wall -Wextra -I"$ROOT/lib/core" -I"$ROOT/lib/hal" \
       -c "$ROOT/lib/core/frame_codec.cpp" -o "$OUT/frame_codec.o" 2>"$OUT/sup.log"; then
  echo "SUPPORT BUILD FAILED (lib/core/frame_codec.cpp):"; sed 's/^/    /' "$OUT/sup.log" | head -20; exit 1
fi

# build_probe <header-shadow-dir-or-empty> <header-path-whose-hash-is-compiled-in> <out-binary>
# ⛔ A mutated header is handed in as an include DIR that SHADOWS `src/`, so a control never writes to the repo.
build_probe() {
  local shadow=$1 hashsrc=$2 bin=$3
  local pre=() h
  [ -n "$shadow" ] && pre=(-I"$shadow")
  h=$(fnv "$hashsrc") || return 1
  : > "$OUT/build.log"
  "$CXX" "${STD[@]}" "${WARN[@]}" "${DEFS[@]}" -DPROBE_BLE_HDR_HASH="$h" \
       "${pre[@]}" "${INCS[@]}" -c "$HERE/probe_main.cpp" -o "$OUT/probe_main.o" 2>>"$OUT/build.log" \
    && "$CXX" "$OUT/probe_main.o" "$OUT/frame_codec.o" -o "$bin" 2>>"$OUT/build.log"
}

rc=0
if ! build_probe "" "$HDR" "$OUT/probe"; then
  echo "PROBE BUILD FAILED — the real src/ header did not host-compile:"
  sed 's/^/    /' "$OUT/build.log" | head -30
  exit 1
fi

# ⛔⛔ THE PIPELINE RUNS BARE AND `PIPESTATUS` IS READ ON THE VERY NEXT LINE. A trailing `|| true` would REPLACE
#     `PIPESTATUS` with `(0)` and read a failing probe as a passing one — the sibling probe's measured defect.
"$OUT/probe" "$HDR" 2>&1 | tee "$OUT/probe.out"
probe_rc=${PIPESTATUS[0]}
n_checks=$(grep -cE '^  (ok|FAIL) ' "$OUT/probe.out")
n_probe_fail=$(grep -c '^  FAIL ' "$OUT/probe.out")

# ================================================================================================================
# NEGATIVE CONTROLS
# ================================================================================================================
n_ctl=0; n_bad=0

classify_control() {   # classify_control <exit-code> <fail-line-count> -> red | passes | abnormal | silent
  local crc=$1 fails=$2
  if   [ "$crc" -eq 0 ];    then printf 'passes'    # the mutant satisfied every check — the property is not measured
  elif [ "$crc" -ne 1 ];    then printf 'abnormal'  # a signal / abort / any exit the probe cannot produce = a crash
  elif [ "$fails" -eq 0 ];  then printf 'silent'    # "failed" without naming one failure — nothing to attribute
  else                           printf 'red'
  fi
}

verdict_of() {   # verdict_of <label> <binary> <header-path-to-inspect>
  local label=$1 bin=$2 hdr=$3
  local rc_m=0
  bash -c '"$1" "$2"; exit $?' _ "$bin" "$hdr" >"$OUT/mutant.out" 2>&1 || rc_m=$?
  local fails; fails=$(grep -c '^  FAIL ' "$OUT/mutant.out")
  local verdict; verdict=$(classify_control "$rc_m" "$fails")
  case "$verdict" in
    passes)   n_bad=$((n_bad+1)); printf '  FAIL %s — the probe still PASSES against the mutant (measures nothing)\n' "$label" ;;
    abnormal) n_bad=$((n_bad+1)); printf '  FAIL %s — the mutant DIED (exit %s, %s failure(s)); a crash measures nothing\n' "$label" "$rc_m" "$fails" ;;
    silent)   n_bad=$((n_bad+1)); printf '  FAIL %s — non-zero exit with ZERO named failures; nothing to attribute\n' "$label" ;;
    red)      n_ctl=$((n_ctl+1)); printf '  ok   %s -> RED (%s check(s) failed: %s)\n' "$label" "$fails" \
                                     "$(grep '^  FAIL ' "$OUT/mutant.out" | sed 's/^  FAIL \([A-Z0-9]*\).*/\1/' | tr '\n' ' ')" ;;
  esac
}

# ctl_hdr <label> <sed-script> — mutate the HEADER, rebuild against the mutant, inspect the mutant.
# ⓘ The mutant's OWN hash is compiled in and the mutant's OWN path is inspected, so the A1 integrity pin stays
#   GREEN and the control is attributable to the behaviour/structure it broke — not to the pin firing on everything.
ctl_hdr() {
  local label=$1 script=$2
  mkdir -p "$OUT/hdr"
  sed "$script" "$HDR" > "$OUT/hdr/device_ble.h"
  if cmp -s "$HDR" "$OUT/hdr/device_ble.h"; then
    n_bad=$((n_bad+1)); printf '  FAIL %s — the mutation changed NOTHING (VACUOUS)\n' "$label"; return
  fi
  if ! build_probe "$OUT/hdr" "$OUT/hdr/device_ble.h" "$OUT/mutant.bin"; then
    n_bad=$((n_bad+1))
    printf '  FAIL %s — the mutant does not COMPILE, so the probe never ran against it:\n' "$label"
    sed 's/^/        /' "$OUT/build.log" | head -8; return
  fi
  verdict_of "$label" "$OUT/mutant.bin" "$OUT/hdr/device_ble.h"
}

# The checked entropy header is copied beside the copied transport header: quoted includes then resolve
# to the mutated provider, never the live src/ sibling. Every control still compiles and executes the real code.
ctl_rng() {
  local label=$1 script=$2
  mkdir -p "$OUT/rng"
  cp "$HDR" "$OUT/rng/device_ble.h"
  sed "$script" "$ROOT/src/device_rng.h" > "$OUT/rng/device_rng.h"
  if cmp -s "$ROOT/src/device_rng.h" "$OUT/rng/device_rng.h"; then
    n_bad=$((n_bad+1)); echo "  FAIL $label VACUOUS"; return
  fi
  if ! build_probe "$OUT/rng" "$OUT/rng/device_ble.h" "$OUT/rng.bin"; then
    n_bad=$((n_bad+1)); echo "  FAIL $label does not compile"; head -8 "$OUT/build.log"; return
  fi
  verdict_of "$label" "$OUT/rng.bin" "$OUT/rng/device_ble.h"
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
      echo "  ok   N0 a real SIGSEGV classifies as UNUSABLE, and 1/3 red · 0/0 passes · 1/0 silent still discriminate"
      n_ctl=$((n_ctl+1))
    else
      echo "  FAIL N0 the control classifier does not hold"; n_bad=$((n_bad+1))
    fi
  else
    echo "  FAIL N0 the crash repro did not build — the [[B237]] rule is unmeasured"; n_bad=$((n_bad+1))
  fi

  # ---- N1: THE OLD CAPACITY IS RESTORED. This is the pre-0f firmware, and it must fail the binding positive —
  #          otherwise the whole slice measures nothing.
  ctl_hdr 'N1 the 160-byte literal buffer is restored' \
      's|char                       g_line\[kLineStorageBytes\];|char                       g_line[160];|'

  # ---- N2: THE DERIVATION IS REPLACED BY THE RIGHT ANSWER, HARD-CODED. Behaviour is identical TODAY and rots the
  #          moment a flag, a verb or `gw_env_max_hops` moves — the exact defect this slice exists to prevent.
  ctl_hdr 'N2 the derived extent is replaced by a naked literal of the same value' \
      's|char                       g_line\[kLineStorageBytes\];|char                       g_line[275];|'

  # ---- N3: THE ADMISSION EDGE IS OFF BY ONE — the classic `<=` slip, which admits one byte too many and leaves no
  #          room for the NUL `dispatch_current_line()` writes.
  ctl_hdr 'N3 the intake admits `g_pos == sizeof(g_line)` (one byte too many)' \
      's|else if (g_pos < sizeof(g_line) - 1)|else if (g_pos < sizeof(g_line))    |'

  # ---- N4: THE LOUD REFUSAL IS BYPASSED. The over-long line is then SILENTLY TRUNCATED into a real command — the
  #          C2 fail-loud rule inverted, and the one shape where the operator is told nothing at all.
  ctl_hdr 'N4 the overflow refusal branch never fires (silent truncation)' \
      's|    if (g_overflow) {  |    if (false) {       |'

  # ---- N5: THE REFUSAL IS EMITTED *AND* THE TRUNCATED PREFIX IS DISPATCHED — "a success that isn'"'"'t": a real
  #          command built from a line the node already declared too long.
  ctl_hdr 'N5 an overflowed line is refused AND its truncated prefix is dispatched' \
      's|        g_overflow = false; g_pos = 0; return;|        g_overflow = false;|'

  # ---- N6: THE INTAKE NEVER RESETS AFTER A REFUSAL. One over-long line wedges the BLE console permanently, and
  #          nothing but a reboot recovers it.
  ctl_hdr 'N6 the intake state is not reset after a refusal (the console wedges)' \
      's|        g_overflow = false; g_pos = 0; return;|        return;|'

  ctl_rng 'E8 failed pool reports success' 's|sd_rand_application_bytes_available_get(\&avail) != NRF_SUCCESS) return false;|sd_rand_application_bytes_available_get(\&avail) != NRF_SUCCESS) return true;|'
  ctl_rng 'E9 failed vector reports success' 's|sd_rand_application_vector_get(out + got, take) != NRF_SUCCESS) return false;|sd_rand_application_vector_get(out + got, take) != NRF_SUCCESS) return true;|'
  ctl_rng 'E10 stalled pool reports success' 's|return got == n;|return true;|'

  ctl_hdr 'N8 direct reply bypasses the bounded writer' \
      's|if (n) tx_line(g_out, n);|if (n) g_bleuart.write(reinterpret_cast<const uint8_t*>(g_out), n);|'

  # ---- N7: THE SOURCE-INTEGRITY PIN ITSELF. The CLEAN binary (compiled against the live header) is pointed at a
  #          DIFFERENT text. "Compile a copy, inspect the live tree" is the classic worthless probe, and A1 is the
  #          only thing standing between this probe and that shape — so A1 must be able to fail.
  sed '1a // probe control N7: this text is deliberately NOT the compiled header' "$HDR" > "$OUT/other_ble.h"
  if cmp -s "$HDR" "$OUT/other_ble.h"; then
    n_bad=$((n_bad+1)); echo "  FAIL N7 — the mutation changed NOTHING (VACUOUS)"
  else
    verdict_of 'N7 the compiled header and the inspected header differ (source-integrity pin)' \
               "$OUT/probe" "$OUT/other_ble.h"
  fi
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
echo "probe: $n_checks checks against the REAL device_ble.h intake, $n_probe_fail failed (pin $PIN_CHECKS)"
[ "$probe_rc" -eq 0 ]             || { echo "  !! the clean probe EXITED $probe_rc"; rc=1; }
[ "$n_probe_fail" -eq 0 ]         || { echo "  !! the clean probe reported $n_probe_fail failed check(s)"; rc=1; }
[ "$n_checks" -eq "$PIN_CHECKS" ] || {
  echo "  !! CHECK COUNT MOVED: $n_checks, pinned $PIN_CHECKS — a check was added or deleted."
  echo "     If deliberate, update PIN_CHECKS at the top of this file WITH its derivation."; rc=1; }

if [ "${1:-}" != "--no-neg" ]; then
  echo "controls: $n_ctl verified / $n_bad unusable (pin $PIN_CONTROLS)"
  [ "$n_bad" -eq 0 ]                 || rc=1
  [ "$n_ctl" -eq "$PIN_CONTROLS" ]   || {
    echo "  !! CONTROL COUNT MOVED: $n_ctl, pinned $PIN_CONTROLS — a negative control was added or deleted."
    echo "     If deliberate, update PIN_CONTROLS at the top of this file."; rc=1; }
  [ "$rc" -eq 0 ] && echo "PASS" || echo "FAIL"
else
  # ⛔ `--no-neg` MUST NEVER PRINT AN ORDINARY `PASS`. Without the controls this run cannot say the checks CAN
  #    fail, which is the sibling probes' documented trap.
  if [ "$rc" -eq 0 ]; then echo "PROBE-ONLY — NOT A GATE (controls skipped; run without --no-neg to gate)"
  else                     echo "FAIL"; fi
fi
exit $rc
