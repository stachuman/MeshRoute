#!/usr/bin/env bash
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# §remote-admin v2 SLICE 1/1b — THE FEATURE-MATRIX GATE for the REAL `lib/core/mr_features.h`, plus the
# FIRST-CONSUMER OWNERSHIP CONTRACT for the pair it derives.
#
# WHY THIS EXISTS. Slice 1 added `MR_FEAT_RADMIN_CLIENT` / `MR_FEAT_RADMIN_ACCEPT`. Nothing else in the tree can see
# their VALUES: `pio test -e native` compiles ONE configuration and the simulator two, and all three are HOST builds
# — none is a board, and a BOARD is where the whole R-RA-17 exclusivity rule lives. The thirteen board environments
# are compiled by the board gate, but that gate reads RAM and flash, not flag VALUES. So this runner is the only
# instrument that measures what the derivation actually does.
# ⚠ CORRECTED 2026-09-06 (SLICE 1b), old claim visible: this header said slice 1 added the pair *"and, deliberately,
#   NO consumer"* and that *"an unused macro moves neither"* RAM nor flash. Both are WITHDRAWN — 1b is the FIRST
#   CONSUMER (`lib/core/node_mac_rx.cpp` compiles the two capability-owned remote receive entry points and their one
#   call site under these macros), so the flags now DO move board flash, and the zero-consumer census S3 used to
#   assert has become the ownership CONTRACT below. The census was not deleted; it was aimed one slice further on.
#
# ★★ IT MUST REMAIN IN THE REPOSITORY AND BE COMMITTED WITH THIS SLICE, for the reason its sibling probes state at
#    this spot: this project has already LOST a proven 33-assert scenario to a session scratchpad
#    ([[meshroute-agent-scratchpad-is-volatile]]). A recipe in a note is not a storage location.
#    ⓘ Verify tracked-ness with `git ls-files`, never from this comment.
#
# WHAT IS MEASURED, said plainly:
#   • NINE configuration cells, each host-compiling the REAL header and EXECUTING `probe_main.cpp`, which asserts
#     the six pre-existing MR_FEAT_* values AND the two new ones against the ruled R-RA-26 matrix below.
#   • The env -> cell mapping is DERIVED (`envmap.py`, through PlatformIO's own resolver + the simulator's
#     CMakeLists), so a configuration nobody builds cannot masquerade as coverage and a new env cannot hide.
#   • The three BOARD-ONLY production refusals, each attacked with its own invalid fixture and each DELETED in
#     isolation to prove it is the thing doing the refusing.
#   • The SLICE-1b OWNERSHIP CONTRACT (`ownership.py`): which files name the pair, which sites inside them, which
#     capability guards each owned symbol, that the real router calls the pure decision with the two macros in the
#     declared order, and that nothing is legacy-widened with `MR_FEAT_REMOTE_MGMT` — each with its own control.
# ⛔ NOT MEASURED HERE: any runtime remote-admin BEHAVIOUR. This runner reads configuration and source; the RX drive
#   is `test/test_node_r3.cpp` §radmin-1b/1..5 and the compile-out is the per-board preprocessing evidence.
#   ⚠ CORRECTED 2026-09-06, old claim visible: this line read *"There is none in slice 1 — the pair has no consumer."*
#
# ⛔⛔ THE SLICE-1 CLASSIFICATION EXCEPTION, DECLARED BEFORE ANY CONTROL RUNS (see the table this script prints).
#     For the four PRODUCTION-REFUSAL controls a COMPILE FAILURE IS THE EXPECTED RED — the exact opposite of every
#     other probe in this repository, where a build failure means the probe never ran. That inversion is only safe
#     because the refusal is attributed to a diagnostic string EXTRACTED FROM THE PRODUCTION HEADER: a missing
#     compiler, a syntax error, an unrelated `#error` or a crash all classify as NOT-a-refusal, and X1..X4 execute
#     exactly those cases to prove the classifier can tell them apart.
#
# USAGE:  tools/probe_features/run.sh            # matrix + ALL controls (the controls run BY DEFAULT)
#         tools/probe_features/run.sh --no-neg   # matrix only — NOT a gate, use only while iterating
# Env:    CXX=...            host compiler (default g++)
#         MR_LUS_SRC=...     simulator source checkout, for the env-map derivation
#         MR_PROBE_DROP=cell|check|control   ⛔ the WRAPPER'S SABOTAGE SWITCH, never for ordinary use: it drops one
#                            configuration / one assertion / one control, and the pins below MUST then fail the run.
#                            `tools/test_probe_features.py` executes all three, because "dropping something cannot
#                            preserve PASS" is a property that cannot be proved by reading a script.

set -uo pipefail
cd "$(dirname "$0")" || exit 1
ROOT=$(cd ../.. && pwd)          # ★ absolute — a relative path in a cwd-resetting shell silently measured nothing
HERE=$(pwd)                      #   once already ([[B82]]). Never make these relative.
CXX=${CXX:-g++}
DROP=${MR_PROBE_DROP:-}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

HDR="$ROOT/lib/core/mr_features.h"     # THE FILE UNDER TEST — every control mutates a COPY of it
MUTATE="$HERE/mutate.py"
OWNERSHIP="$HERE/ownership.py"       # §slice 1b: the first-consumer ownership contract (its own checks + controls)
STD=(-std=gnu++2a -O0)
WARN=(-Wall -Wextra -Werror)

# ================================================================================================================
# ★★★ THE THREE PINNED COUNTS — the `PIN_CASES` idiom from `tools/probe_ui_model_mutations.py`, applied here for
#     the same reason it exists there: WITHOUT A PIN, DELETING A CHECK IS INVISIBLE. A run that reports "0 failed"
#     over 3 surviving checks is indistinguishable, in its exit code and its summary line, from one that ran 97.
#
# PIN_CELLS = 9, derived from the ruled matrix: 3 board ROLES x 2 OLED values (both values are LIVE in the current
#     env matrix — `envmap.py` E7 derives that) + native + lus-normal + lus-gateway.
# PIN_CHECKS = 114, derived by running the clean matrix and counting its `  ok  `/`  FAIL ` lines:
#     S1..S2   the runner's structural pins (3 extracted diagnostics · no -D override surface)                 2
#     E1..E13  `envmap.py`'s derived environment census                                                      13
#     9 cells x (1 source-integrity pin + 8 asserted MR_FEAT_* values)                                       81
#     O1..O13  `ownership.py`'s first-consumer contract (O4 and O6 split per file/owner: O4a-g, O6a-c)       22
#     2 + 13 + 81 + 22 = 118. ✓
#   ⚠ CORRECTED 2026-09-06 (SLICE 1b), old derivation visible: this read *"PIN_CHECKS = 97 … S1..S3 … 3 + 13 + 81"*.
#     S3 (*"exactly ONE production location names the pair"*) is RETIRED BY REPLACEMENT, not by deletion: 1b is the
#     first consumer, so the zero-consumer census became `ownership.py`'s exact site census. 3 - 1 + 18 = +17.
#   ⚠⚠ RE-PINNED 2026-09-06 BY §RADMIN SLICE 3, 114 -> 118, AND THE +4 IS FULLY ATTRIBUTED: **O4 is one check PER
#     APPROVED FILE**, and the approved census grew from THREE files to SEVEN (the target-store console surface —
#     `firmware_commands.{cpp,h}`, `fw_main.cpp`, `firmware_help.h`). O4a-c became O4a-g ⇒ +4 checks, and ⛔ NOTHING
#     ELSE MOVED: S1..S2 still 2, E1..E13 still 13, the 9 cells still 81, O1/O2/O3/O5/O6a-c/O7a-b/O8..O13 unchanged.
#     18 + 4 = 22. Every previously-pinned check is still executed and still green — this is a GROWTH, not a re-base.
# PIN_CONTROLS = 52: A1..A4 refusals · B1..B6 executable matrix mutations · C1..C5 guard removal/scope ·
#     X1..X4 the controls-of-the-controls · W-* the 27 ownership violations · Y0..Y5 the ownership
#     controls-of-controls (a green baseline, a benign edit, multi-match, vacuous, unreadable, tree integrity).
#     4 + 6 + 5 + 4 + 27 + 6 = 52. ✓
#     ⚠ RE-DERIVED 2026-09-06 BY §RADMIN SLICE 4, 44 -> 52. Every prior control is PRESERVED; the +8 are one per
#     NEW CONTROLLER owner boundary, in the SAME five shapes the slice-3 six take: the controller router arm's
#     gate DELETED, the R-RA-30 BLE split's gate DELETED, the controller boot call's gate legacy-WIDENED, the two
#     controller help names INVERTED onto ACCEPT, ★ `do_regen`'s CLIENT ADMISSION inverted onto ACCEPT, a
#     DUPLICATE guard in the controller boot-wrapper header, and BOTH new pure headers acquiring a capability
#     macro (the keyring's and the verb header's — the latter matters because the BLE guard's extractor compiles
#     that file UNGATED).
#     (historical:)  PIN_CONTROLS = 44 = 4 + 6 + 5 + 4 + 19 + 6.  ✓  (was 19 before slice 1b and 38 before §RADMIN slice 3 — every prior control
#     is PRESERVED; the +6 are one per NEW owner boundary: the router arm's gate DELETED, the BLE refusal's gate
#     DELETED, the boot call's gate legacy-WIDENED, the help names INVERTED onto the CLIENT capability, a
#     DUPLICATE guard in the boot-wrapper header, and a PURE SERVICE HEADER acquiring a capability macro.)
PIN_CELLS=9
PIN_CHECKS=118
PIN_CONTROLS=52

# ---- the tree must not move ------------------------------------------------------------------------------------
# ⛔ SPELLED ONCE, IN A FUNCTION (the sibling probe's lesson: two `cat` lists drifted apart and produced a FALSE RED
#    on a tree nothing had touched).
md5_sources() {
  cat "$HDR" "$HERE/probe_main.cpp" "$HERE/envmap.py" "$MUTATE" "$OWNERSHIP" "$ROOT/platformio.ini" \
    | md5sum | cut -d' ' -f1
}
MD5_BEFORE=$(md5_sources)

# FNV-1a-64 of a file — the runner's half of the source-integrity pin. `probe_main.cpp` recomputes it in C++ over
# the file it is handed, so the two implementations have to agree; that is what forbids "compile a copy, inspect
# the tree".
fnv() { python3 -c '
import sys
h = 14695981039346656037
for b in open(sys.argv[1], "rb").read():
    h ^= b; h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
print("0x%016xULL" % h)' "$1"; }

n_checks_extra=0
n_fail_extra=0
say_ok()   { n_checks_extra=$((n_checks_extra+1)); printf '  ok   %s\n' "$1"; }
say_fail() { n_checks_extra=$((n_checks_extra+1)); n_fail_extra=$((n_fail_extra+1)); printf '  FAIL %s\n' "$1"; }

echo "== §remote-admin v2 slice 1 — the mr_features.h configuration matrix (the REAL lib/core header) =="
echo "   header under test : $HDR"
echo "   md5(header)       : $(md5sum "$HDR" | cut -d' ' -f1)"
echo "   compiler          : $CXX ($("$CXX" --version 2>/dev/null | head -1))"
[ -n "$DROP" ] && echo "   ⛔ MR_PROBE_DROP=$DROP — SABOTAGE MODE, this run MUST fail"

# ================================================================================================================
# ★★★ THE CONTROL CLASSIFICATION — PRINTED BEFORE ANY CONTROL RUNS, because a classification decided after seeing
#     the outcome is not a classification.
# ================================================================================================================
cat <<'CLASS'

== control classification (declared up-front; nothing below may be reclassified after the fact) ==
  class A — PRODUCTION COMPILE-TIME REFUSAL (A1..A4)
      measured RED = the declared INVALID configuration fails to compile AT the intended mr_features.h diagnostic
                     (matched against text EXTRACTED from the production header), AND its legal sibling
                     configuration still compiles and runs green under the same mutant.
      NEVER a RED  = a missing compiler, a syntax error, an unrelated #error, a promoted warning, a signal,
                     a timeout, or a probe-owned assertion.
  class B — EXECUTABLE MATRIX MUTATION (B1..B6)
      measured RED = the mutated header BUILDS, the driver RUNS, and it names the expected wrong-value assertion;
                     a collateral cell stays green, so the mutation is scoped rather than global.
      NEVER a RED  = any build failure or abnormal exit without the named failed assertion.
  class C — GUARD REMOVAL / GUARD SCOPE (C1..C5)
      measured RED = deleting ONE production check on an isolated copy makes its own invalid fixture COMPILE
                     (proving that check, and no other, was refusing it); C5 instead WIDENS the board-only fence
                     and the legitimate host {1,1} must then be refused. The enclosing gate rejects the changed
                     outcome for the named reason.
      NEVER a RED  = a missing/multiple mutation match, an unexplained failure, or an unchanged outcome.
  class X — THE CONTROLS OF THE CONTROLS (X1..X4)
      a broken toolchain, an unrelated syntax error and an unrelated #error must each classify as NOT-a-refusal,
      and the classifier's own truth table must discriminate. Plus the three pins above, which the wrapper
      sabotages (MR_PROBE_DROP) to prove that dropping a cell / a check / a control cannot preserve PASS.
  class W — SLICE-1b OWNERSHIP VIOLATION (W-*)          [ownership.py --controls; NOTHING is compiled]
      measured RED = ONE exact-match edit on an ISOLATED COPY of the sources makes the ownership checker REJECT
                     that copy, AND the NAMED check(s) are the ones that reject it (reported by id).
      NEVER a RED  = a nonzero exit alone, a rejection by some other check only, a find text that matches zero or
                     more than one time (that is an INSTRUMENT ERROR, ctl-BAD), or an unreadable source.
  class Y — THE CONTROLS OF THE OWNERSHIP CONTROLS (Y0..Y5)
      Y0 the untouched copy must pass every check first (else no rejection below means anything) · Y1 a benign
      comment edit must NOT be rejected · Y2 a multi-match and Y3 a vacuous find must be refused as instrument
      errors · Y4 an unreadable source must raise a GATE ERROR, never a pass · Y5 the shared checkout must be
      byte-identical before and after.
CLASS
echo

# ================================================================================================================
# THE RULED MATRIX (R-RA-26). Expectation order: TEAM MOBILE MOBILE_HOST GATEWAY OLED REMOTE_MGMT CLIENT ACCEPT.
# ⛔ These values are TRANSCRIBED FROM THE RULING, not read back from the header — that is the whole point.
# ⓘ Each cell carries the FEATURE-RELEVANT projection of a real flag set. `envmap.py` E13 derives that the only
#   MR_FEAT_* an environment ever sets is MR_FEAT_OLED, which is what makes the projection complete rather than
#   convenient; E1..E12 tie every one of the 14 environments and both simulator variants to a cell below.
# ================================================================================================================
ALL_CELLS=(board_mobile_oled0 board_mobile_oled1 board_gateway_oled0 board_gateway_oled1 \
           board_static_oled0 board_static_oled1 host_native host_lus_normal host_lus_gateway)

cell_defs() {
  case "$1" in
    board_mobile_oled0)  echo "-DARDUINO=100 -DMR_PROFILE_MOBILE" ;;
    board_mobile_oled1)  echo "-DARDUINO=100 -DMR_PROFILE_MOBILE -DMR_FEAT_OLED=1" ;;
    board_gateway_oled0) echo "-DARDUINO=100 -DMR_PROFILE_GATEWAY -DMR_N_LAYERS=2 -DMR_GATEWAY_BUILD=1" ;;
    board_gateway_oled1) echo "-DARDUINO=100 -DMR_PROFILE_GATEWAY -DMR_N_LAYERS=2 -DMR_GATEWAY_BUILD=1 -DMR_FEAT_OLED=1" ;;
    board_static_oled0)  echo "-DARDUINO=100" ;;
    board_static_oled1)  echo "-DARDUINO=100 -DMR_FEAT_OLED=1" ;;
    host_native)         echo "-DMESHROUTE_NATIVE=1 -DMR_N_LAYERS=2" ;;
    host_lus_normal)     echo "" ;;
    host_lus_gateway)    echo "-DMESHROUTE_NS=meshroute_gw -DMR_N_LAYERS=2 -DMR_GATEWAY_BUILD=1 -DMR_CAP_CHANNEL_BUFFER=8 -DMR_CAP_DEFERRED_SENDS=16" ;;
    *) return 1 ;;
  esac
}
cell_exp() {
  case "$1" in
    board_mobile_oled0)  echo "1 1 1 1 0 0 1 0" ;;
    board_mobile_oled1)  echo "1 1 1 1 1 0 1 0" ;;
    board_gateway_oled0) echo "0 0 1 1 0 1 0 1" ;;
    board_gateway_oled1) echo "0 0 1 1 1 1 0 1" ;;
    board_static_oled0)  echo "1 1 1 1 0 1 0 1" ;;
    board_static_oled1)  echo "1 1 1 1 1 1 0 1" ;;
    host_native)         echo "1 1 1 1 0 1 1 1" ;;
    host_lus_normal)     echo "1 1 1 1 0 1 1 1" ;;
    host_lus_gateway)    echo "1 1 1 1 0 1 1 1" ;;
    *) return 1 ;;
  esac
}

# compile_cell <cell> <shadow-dir-or-empty> <header-whose-hash-is-compiled-in> <out-binary>
compile_cell() {
  local cell=$1 shadow=$2 hashsrc=$3 bin=$4
  local h; h=$(fnv "$hashsrc") || return 2
  local pre=(); [ -n "$shadow" ] && pre=(-I"$shadow")
  local defs; read -r -a defs <<< "$(cell_defs "$cell")"
  local e; read -r -a e <<< "$(cell_exp "$cell")"
  local drop=(); [ "$DROP" = check ] && drop=(-DPROBE_DROP_CHECK)
  : > "$OUT/build.log"
  "$CXX" "${STD[@]}" "${WARN[@]}" "${defs[@]}" "${drop[@]}" \
      -DPROBE_CELL="\"$cell\"" -DPROBE_HDR_HASH="$h" \
      -DEXP_TEAM="${e[0]}" -DEXP_MOBILE="${e[1]}" -DEXP_MOBILE_HOST="${e[2]}" -DEXP_GATEWAY="${e[3]}" \
      -DEXP_OLED="${e[4]}" -DEXP_REMOTE_MGMT="${e[5]}" -DEXP_RADMIN_CLIENT="${e[6]}" -DEXP_RADMIN_ACCEPT="${e[7]}" \
      "${pre[@]}" -I"$ROOT/lib/core" "$HERE/probe_main.cpp" -o "$bin" 2>>"$OUT/build.log"
}

# ---- S1..S3: the runner's structural pins, and the DERIVED diagnostics the classifier matches on ---------------
mapfile -t DIAGS < <(sed -n 's/^#    error "\(.*\)"$/\1/p' "$HDR")
if [ "${#DIAGS[@]}" -eq 3 ] && [ "${DIAGS[0]}" != "${DIAGS[1]}" ] && [ "${DIAGS[1]}" != "${DIAGS[2]}" ] \
   && [ "${DIAGS[0]}" != "${DIAGS[2]}" ]; then
  say_ok "S1 the header declares exactly 3 distinct board-only #error diagnostics (extracted, not typed here)"
  printf '       [1] %s\n       [2] %s\n       [3] %s\n' "${DIAGS[0]}" "${DIAGS[1]}" "${DIAGS[2]}"
else
  say_fail "S1 expected exactly 3 distinct '#    error' diagnostics in $HDR, found ${#DIAGS[@]}"
  DIAGS=("__no_such_diagnostic_1__" "__no_such_diagnostic_2__" "__no_such_diagnostic_3__")
fi
DIAG_BOTH=${DIAGS[0]}; DIAG_NEITHER=${DIAGS[1]}; DIAG_CONSISTENCY=${DIAGS[2]}

if grep -q '^#ifndef MR_FEAT_RADMIN' "$HDR"; then
  say_fail "S2 the pair has an #ifndef override surface — R-RA-26 forbids dialling an invalid pair in from an env"
else
  say_ok "S2 the pair is DERIVED with no #ifndef override surface (an invalid pair cannot be dialled in by -D)"
fi

# ---- O1..O13: the SLICE-1b FIRST-CONSUMER OWNERSHIP CONTRACT (replaces slice 1's S3 zero-consumer pin) --------
# ⛔ S3 IS RETIRED BY REPLACEMENT, and its own wrapper said this is how it must go: *"WHEN THE FIRST CONSUMER LANDS
#    (a later slice), `test_the_pair_has_no_consumer` is the assertion that must be DELIBERATELY updated — that is
#    its job, not an obstacle to route around."* It read: `owners=$(grep -rl 'MR_FEAT_RADMIN' lib src test)` and
#    required `owners == $HDR`. `ownership.py` asserts the strictly stronger successor: the exact FILE census, the
#    exact SITE census inside each allowed file, the per-owner capability guard, the real router's call and argument
#    ORDER, no legacy widening, no `none` arm and no test-as-owner.
echo
echo "== the slice-1b first-consumer ownership contract (the REAL lib/core sources) =="
python3 "$OWNERSHIP" --root "$ROOT" 2>&1 | tee "$OUT/own.out"
own_rc=${PIPESTATUS[0]}

# ---- E1..E13: the derived environment census -------------------------------------------------------------------
echo
echo "== derived environment -> cell mapping (platformio.ini via pio, + the simulator's CMakeLists) =="
python3 "$HERE/envmap.py" 2>&1 | tee "$OUT/envmap.out"
envmap_rc=${PIPESTATUS[0]}

# ---- the nine configuration cells --------------------------------------------------------------------------------
echo
echo "== the ruled configuration matrix: nine cells, each host-compiled and host-RUN =="
CELLS=("${ALL_CELLS[@]}")
if [ "$DROP" = cell ]; then
  unset 'CELLS[${#CELLS[@]}-1]'                       # the wrapper's sabotage: one configuration silently missing
fi
n_cells=0
matrix_rc=0
: > "$OUT/matrix.out"
for cell in "${CELLS[@]}"; do
  if ! compile_cell "$cell" "" "$HDR" "$OUT/probe_$cell"; then
    echo "MATRIX BUILD FAILED — the real lib/core header did not host-compile for cell $cell:"
    sed 's/^/    /' "$OUT/build.log" | head -30
    matrix_rc=1
    continue
  fi
  n_cells=$((n_cells+1))
  "$OUT/probe_$cell" "$HDR" 2>&1 | tee -a "$OUT/matrix.out"
  [ "${PIPESTATUS[0]}" -eq 0 ] || matrix_rc=1
done
n_checks=$(( $(grep -cE '^  (ok|FAIL) ' "$OUT/matrix.out") + $(grep -cE '^  (ok|FAIL) ' "$OUT/envmap.out") \
             + $(grep -cE '^  (ok|FAIL) ' "$OUT/own.out") + n_checks_extra ))
n_fail=$(( $(grep -c '^  FAIL ' "$OUT/matrix.out") + $(grep -c '^  FAIL ' "$OUT/envmap.out") \
           + $(grep -c '^  FAIL ' "$OUT/own.out") + n_fail_extra ))

# ================================================================================================================
# CONTROLS
# ================================================================================================================
n_ctl=0; n_bad=0

# classify_refusal <exit-code> <compiler-log> -> refusal | accepted | unrelated | abnormal
classify_refusal() {
  local rc=$1 log=$2
  if   [ "$rc" -eq 0 ];   then printf 'accepted'          # the invalid configuration COMPILED — nothing refused it
  elif [ "$rc" -ge 128 ]; then printf 'abnormal'          # a signal: the compiler died, it did not diagnose
  elif [ "$(matched_diag "$log")" = none ]; then printf 'unrelated'   # ⛔ a failure that is NOT a policy refusal
  else printf 'refusal'
  fi
}
matched_diag() {   # -> both | neither | consistency | none
  local log=$1
  grep -qF -- "$DIAG_BOTH"        "$log" && { printf 'both';        return; }
  grep -qF -- "$DIAG_NEITHER"     "$log" && { printf 'neither';     return; }
  grep -qF -- "$DIAG_CONSISTENCY" "$log" && { printf 'consistency'; return; }
  printf 'none'
}

# mutate <label> <out-dir> <mutate.py args...> — writes $OUT/<dir>/mr_features.h from the CURRENT copy in that dir
# (or from the real header when the directory is fresh). Verifies the single-match assertion itself.
mutate() {
  local label=$1 dir=$2; shift 2
  local src="$OUT/$dir/mr_features.h"
  mkdir -p "$OUT/$dir"
  [ -f "$src" ] || cp "$HDR" "$src"
  if ! python3 "$MUTATE" --in "$src" --out "$src.new" "$@" > "$OUT/mutate.log" 2>&1; then
    n_bad=$((n_bad+1)); printf '  FAIL %s — the mutation is not single-match/vacuous: %s\n' \
        "$label" "$(cat "$OUT/mutate.log")"; return 1
  fi
  mv "$src.new" "$src"
  return 0
}

# ctl_refusal <label> <dir> <invalid-cell> <expected-diag> <legal-sibling-cell>
ctl_refusal() {
  local label=$1 dir=$2 bad=$3 want=$4 good=$5
  local hdr="$OUT/$dir/mr_features.h" rc=0
  compile_cell "$bad" "$OUT/$dir" "$hdr" "$OUT/ctl.bin" || rc=$?
  cp "$OUT/build.log" "$OUT/$dir.log"
  local verdict; verdict=$(classify_refusal "$rc" "$OUT/$dir.log")
  local got; got=$(matched_diag "$OUT/$dir.log")
  if [ "$verdict" != refusal ]; then
    n_bad=$((n_bad+1))
    printf '  FAIL %s — %s (exit %s), NOT a production refusal:\n' "$label" "$verdict" "$rc"
    sed 's/^/        /' "$OUT/$dir.log" | head -6; return
  fi
  if [ "$got" != "$want" ]; then
    n_bad=$((n_bad+1))
    printf '  FAIL %s — refused at the `%s` diagnostic, expected `%s`\n' "$label" "$got" "$want"; return
  fi
  # the legal sibling must still compile AND run green under the very same mutant, or the control proved only
  # that the mutant is broken everywhere.
  local sib_rc=0
  compile_cell "$good" "$OUT/$dir" "$hdr" "$OUT/ctl_sib.bin" || sib_rc=$?
  if [ "$sib_rc" -ne 0 ] || ! "$OUT/ctl_sib.bin" "$hdr" > "$OUT/sib.out" 2>&1; then
    n_bad=$((n_bad+1))
    printf '  FAIL %s — the LEGAL sibling %s does not build+run green under the same mutant\n' "$label" "$good"
    sed 's/^/        /' "$OUT/$dir.log" "$OUT/sib.out" 2>/dev/null | head -6; return
  fi
  n_ctl=$((n_ctl+1))
  printf '  ok   %s -> REFUSED at `%s` (exit %s); legal sibling %s still green\n' "$label" "$got" "$rc" "$good"
}

# ctl_exec <label> <dir> <cell> <expected-failing-flag> <green-sibling-cell>
ctl_exec() {
  local label=$1 dir=$2 cell=$3 flag=$4 good=$5
  local hdr="$OUT/$dir/mr_features.h"
  if ! compile_cell "$cell" "$OUT/$dir" "$hdr" "$OUT/ctl.bin"; then
    n_bad=$((n_bad+1))
    printf '  FAIL %s — the mutant does not COMPILE for %s, so the driver never ran against it:\n' "$label" "$cell"
    sed 's/^/        /' "$OUT/build.log" | head -6; return
  fi
  local rc=0
  "$OUT/ctl.bin" "$hdr" > "$OUT/$dir.out" 2>&1 || rc=$?
  local fails; fails=$(grep -c '^  FAIL ' "$OUT/$dir.out")
  if [ "$rc" -ge 128 ]; then
    n_bad=$((n_bad+1)); printf '  FAIL %s — the driver DIED (exit %s); a crash measures nothing\n' "$label" "$rc"; return
  fi
  if [ "$rc" -eq 0 ] || [ "$fails" -eq 0 ]; then
    n_bad=$((n_bad+1)); printf '  FAIL %s — the driver still PASSES against the mutant (measures nothing)\n' "$label"; return
  fi
  if ! grep -q "^  FAIL $cell\.$flag " "$OUT/$dir.out"; then
    n_bad=$((n_bad+1))
    printf '  FAIL %s — failed, but not on %s.%s: %s\n' "$label" "$cell" "$flag" \
        "$(grep '^  FAIL ' "$OUT/$dir.out" | tr '\n' ' ')"; return
  fi
  local sib_rc=0
  compile_cell "$good" "$OUT/$dir" "$hdr" "$OUT/ctl_sib.bin" || sib_rc=$?
  if [ "$sib_rc" -ne 0 ] || ! "$OUT/ctl_sib.bin" "$hdr" > "$OUT/sib.out" 2>&1; then
    n_bad=$((n_bad+1))
    printf '  FAIL %s — collateral: the unrelated cell %s is no longer green, so the mutation is not scoped\n' \
        "$label" "$good"; return
  fi
  n_ctl=$((n_ctl+1))
  printf '  ok   %s -> RED on %s (%s failed: %s); %s still green\n' "$label" "$cell" "$fails" \
      "$(grep '^  FAIL ' "$OUT/$dir.out" | sed "s/^  FAIL $cell\.\([A-Za-z_]*\).*/\1/" | tr '\n' ' ')" "$good"
}

# ctl_guard_accepts <label> <dir> <fixture-cell> — the guard was deleted: the invalid fixture must now COMPILE.
ctl_guard_accepts() {
  local label=$1 dir=$2 cell=$3
  local hdr="$OUT/$dir/mr_features.h" rc=0
  compile_cell "$cell" "$OUT/$dir" "$hdr" "$OUT/ctl.bin" || rc=$?
  if [ "$rc" -ne 0 ]; then
    n_bad=$((n_bad+1))
    printf '  FAIL %s — %s is STILL refused with the guard deleted: another check concealed the removal (%s)\n' \
        "$label" "$cell" "$(matched_diag "$OUT/build.log")"
    sed 's/^/        /' "$OUT/build.log" | head -6; return
  fi
  n_ctl=$((n_ctl+1))
  printf '  ok   %s -> the invalid fixture %s now COMPILES; that guard, and no other, was refusing it\n' "$label" "$cell"
}

if [ "${1:-}" != "--no-neg" ]; then
  echo
  echo "== class A — production compile-time refusals (a COMPILE FAILURE is the declared RED here, and ONLY here) =="

  mutate 'A1' A1 --find '#  define MR_FEAT_RADMIN_CLIENT 0' --replace '#  define MR_FEAT_RADMIN_CLIENT 1' --expect 1 &&
  ctl_refusal 'A1 a BOARD with both endpoints enabled ({1,1}, ACCEPT==REMOTE_MGMT so only exclusivity can fire)' \
      A1 board_static_oled0 both board_mobile_oled0

  mutate 'A2' A2 --find '#  define MR_FEAT_RADMIN_CLIENT 1' --replace '#  define MR_FEAT_RADMIN_CLIENT 0' --expect 2 --nth 1 &&
  ctl_refusal 'A2 a BOARD with neither endpoint ({0,0}, ACCEPT==REMOTE_MGMT so only exclusivity can fire)' \
      A2 board_mobile_oled0 neither board_static_oled0

  mutate 'A3' A3 --find '#if defined(MR_PROFILE_MOBILE)' --replace '#if defined(ARDUINO)' --expect 2 --nth 2 &&
  ctl_refusal 'A3 a STATIC board derived as {1,0} while the legacy switch says managed (exclusivity holds)' \
      A3 board_static_oled0 consistency board_mobile_oled0

  mutate 'A4' A4 --find '#if defined(MR_PROFILE_MOBILE)' --replace '#if defined(MR_PROFILE_GATEWAY)' --expect 2 --nth 2 &&
  ctl_refusal 'A4 a MOBILE board derived as {0,1} while the legacy switch says unmanaged (exclusivity holds)' \
      A4 board_mobile_oled0 consistency board_static_oled0

  echo
  echo "== class B — executable matrix mutations (the mutant BUILDS; the driver names the wrong value) =="

  mutate 'B1' B1 --find '#  define MR_FEAT_RADMIN_CLIENT 1' --replace '#  define MR_FEAT_RADMIN_CLIENT 0' --expect 2 --nth 2 &&
  ctl_exec 'B1 the HOST arm loses CLIENT' B1 host_native MR_FEAT_RADMIN_CLIENT board_mobile_oled0

  mutate 'B2' B2 --find '#  define MR_FEAT_RADMIN_ACCEPT 1' --replace '#  define MR_FEAT_RADMIN_ACCEPT 0' --expect 2 --nth 2 &&
  ctl_exec 'B2 the HOST arm loses ACCEPT' B2 host_lus_normal MR_FEAT_RADMIN_ACCEPT board_static_oled0

  mutate 'B3' B3 --find '#elif defined(ARDUINO)' --replace '#elif defined(ARDUINO) || defined(MR_GATEWAY_BUILD)' --expect 1 &&
  ctl_exec 'B3 MR_GATEWAY_BUILD leaks into the board arm — the SIMULATOR gateway loses CLIENT' \
      B3 host_lus_gateway MR_FEAT_RADMIN_CLIENT host_lus_normal

  mutate 'B4' B4 --find '#elif defined(ARDUINO)' --replace '#elif !defined(MESHROUTE_NATIVE)' --expect 1 &&
  ctl_exec 'B4 the board/host discriminator becomes MESHROUTE_NATIVE — lus-normal is read as a board' \
      B4 host_lus_normal MR_FEAT_RADMIN_CLIENT host_native

  mutate 'B5' B5 \
      --find '#  define MR_FEAT_MOBILE 0                // slice 2: the mobile-MEMBER (roaming endpoint) plane is compiled out (a gateway never registers to a host)' \
      --replace '#  define MR_FEAT_MOBILE 1                // slice 2: the mobile-MEMBER (roaming endpoint) plane is compiled out (a gateway never registers to a host)' --expect 1 &&
  ctl_exec 'B5 the gateway profile stops compiling the mobile plane out (the six OLD flags are live)' \
      B5 board_gateway_oled0 MR_FEAT_MOBILE board_static_oled0

  mutate 'B6' B6 \
      --find '#  define MR_FEAT_OLED 0                  // board UI: OFF by default (opt-in per board); scaffold lands in slice 4' \
      --replace '#  define MR_FEAT_OLED 1                  // board UI: OFF by default (opt-in per board); scaffold lands in slice 4' --expect 1 &&
  ctl_exec 'B6 the OLED default flips to 1 (the OLED column is live, both values measured)' \
      B6 board_static_oled0 MR_FEAT_OLED board_static_oled1

  echo
  echo "== class C — guard removal and guard scope (each fixture is class A's, then ONE guard changes) =="

  mutate 'C1' C1 --find '#  define MR_FEAT_RADMIN_CLIENT 0' --replace '#  define MR_FEAT_RADMIN_CLIENT 1' --expect 1 &&
  mutate 'C1' C1 --delete-block '#  if MR_FEAT_RADMIN_CLIENT && MR_FEAT_RADMIN_ACCEPT' '#  endif' --expect 1 &&
  ctl_guard_accepts 'C1 the both-enabled #error is deleted' C1 board_static_oled0

  mutate 'C2' C2 --find '#  define MR_FEAT_RADMIN_CLIENT 1' --replace '#  define MR_FEAT_RADMIN_CLIENT 0' --expect 2 --nth 1 &&
  mutate 'C2' C2 --delete-block '#  if !MR_FEAT_RADMIN_CLIENT && !MR_FEAT_RADMIN_ACCEPT' '#  endif' --expect 1 &&
  ctl_guard_accepts 'C2 the neither-enabled #error is deleted' C2 board_mobile_oled0

  mutate 'C3' C3 --find '#if defined(MR_PROFILE_MOBILE)' --replace '#if defined(ARDUINO)' --expect 2 --nth 2 &&
  mutate 'C3' C3 --delete-block '#  if MR_FEAT_RADMIN_ACCEPT != MR_FEAT_REMOTE_MGMT' '#  endif' --expect 1 &&
  ctl_guard_accepts 'C3 the ACCEPT==REMOTE_MGMT #error is deleted (static direction)' C3 board_static_oled0

  mutate 'C4' C4 --find '#if defined(MR_PROFILE_MOBILE)' --replace '#if defined(MR_PROFILE_GATEWAY)' --expect 2 --nth 2 &&
  mutate 'C4' C4 --delete-block '#  if MR_FEAT_RADMIN_ACCEPT != MR_FEAT_REMOTE_MGMT' '#  endif' --expect 1 &&
  ctl_guard_accepts 'C4 the ACCEPT==REMOTE_MGMT #error is deleted (mobile direction)' C4 board_mobile_oled0

  mutate 'C5' C5 --find '#if defined(ARDUINO)' --replace '#if 1' --expect 1 &&
  ctl_refusal 'C5 the board-only fence is widened to the HOST — the legitimate {1,1} is now refused' \
      C5 host_native both board_mobile_oled0

  echo
  echo "== class X — the controls of the controls (a failure that is NOT a policy refusal must not score as one) =="

  # X1 — a compiler that cannot build anything. The classifier must NOT read the resulting failure as a refusal.
  x1_rc=0
  _saved_cxx=$CXX; CXX=/bin/false            # ⚠ set/restored explicitly: `VAR=v shell_function` is not reliably scoped
  compile_cell board_static_oled0 "" "$HDR" "$OUT/x1.bin" || x1_rc=$?
  CXX=$_saved_cxx
  if [ "$x1_rc" -ne 0 ] && [ "$(classify_refusal "$x1_rc" "$OUT/build.log")" = unrelated ]; then
    n_ctl=$((n_ctl+1)); echo "  ok   X1 a broken toolchain (CXX=/bin/false) classifies as \`unrelated\`, never a refusal"
  else
    n_bad=$((n_bad+1)); echo "  FAIL X1 a broken toolchain was not separated from a policy refusal (exit $x1_rc)"
  fi

  # X2 — an unrelated SYNTAX ERROR in the header. Same requirement.
  x2_rc=0
  mutate 'X2' X2 --find '#pragma once' --replace 'int probe_control_x2_deliberate_syntax_error( ;' --expect 1 &&
  { compile_cell board_static_oled0 "$OUT/X2" "$OUT/X2/mr_features.h" "$OUT/x2.bin" || x2_rc=$?; }
  if [ "$x2_rc" -ne 0 ] && [ "$(classify_refusal "$x2_rc" "$OUT/build.log")" = unrelated ]; then
    n_ctl=$((n_ctl+1)); echo "  ok   X2 an unrelated syntax error classifies as \`unrelated\`, never a refusal"
  else
    n_bad=$((n_bad+1)); echo "  FAIL X2 an unrelated syntax error was not separated from a policy refusal (exit $x2_rc)"
  fi

  # X3 — an unrelated `#error`. This is the sharpest of the three: it fails the SAME WAY a refusal does.
  x3_rc=0
  mutate 'X3' X3 --find '#pragma once' \
      --replace '#error "probe control X3: an UNRELATED diagnostic that is not a remote-admin policy refusal"' --expect 1 &&
  { compile_cell board_static_oled0 "$OUT/X3" "$OUT/X3/mr_features.h" "$OUT/x3.bin" || x3_rc=$?; }
  if [ "$x3_rc" -ne 0 ] && [ "$(classify_refusal "$x3_rc" "$OUT/build.log")" = unrelated ]; then
    n_ctl=$((n_ctl+1)); echo "  ok   X3 an UNRELATED #error classifies as \`unrelated\` — the refusal is matched by TEXT, not by exit code"
  else
    n_bad=$((n_bad+1)); echo "  FAIL X3 an unrelated #error was scored as a policy refusal (exit $x3_rc)"
  fi

  # X4 — the classifier's own truth table, and the diagnostic attribution it rests on.
  if [ "$DROP" != control ]; then
    printf '%s\n' "error: #error \"$DIAG_CONSISTENCY\"" > "$OUT/x4_real.log"
    printf 'error: something else entirely\n'            > "$OUT/x4_other.log"
    if [ "$(classify_refusal 1 "$OUT/x4_real.log")"  = refusal  ] \
    && [ "$(classify_refusal 1 "$OUT/x4_other.log")" = unrelated ] \
    && [ "$(classify_refusal 0 "$OUT/x4_real.log")"  = accepted  ] \
    && [ "$(classify_refusal 139 "$OUT/x4_real.log")" = abnormal ] \
    && [ "$(matched_diag "$OUT/x4_real.log")" = consistency ]; then
      n_ctl=$((n_ctl+1)); echo "  ok   X4 the classifier discriminates: 1+diag=refusal · 1+other=unrelated · 0=accepted · 139=abnormal"
    else
      n_bad=$((n_bad+1)); echo "  FAIL X4 the control classifier does not hold"
    fi
  fi

  # ---- class W/Y — the SLICE-1b ownership controls (their own isolated copies; the checkout is never touched) ----
  # ⛔ THE CLASSIFICATION FOR THESE IS NOT CLASS A's: an ownership control is measured RED only when the checker
  #    REJECTS the edited copy AND the NAMED check does the rejecting. A nonzero exit is not enough, a rejection by
  #    some other check is not enough, and an edit whose find text does not match EXACTLY ONCE is an INSTRUMENT
  #    ERROR (`ctl-BAD`), never a control. Y0..Y5 are the controls of those controls.
  echo
  echo "== class W/Y — the first-consumer ownership contract's violations and their controls =="
  python3 "$OWNERSHIP" --root "$ROOT" --controls 2>&1 | tee "$OUT/ownctl.out"
  ownctl_rc=${PIPESTATUS[0]}
  n_ctl=$(( n_ctl + $(grep -c '^  ctl-ok ' "$OUT/ownctl.out") ))
  n_bad=$(( n_bad + $(grep -c '^  ctl-BAD ' "$OUT/ownctl.out") ))
  [ "$ownctl_rc" -eq 0 ] || { echo "  !! the ownership control runner exited $ownctl_rc"; n_bad=$((n_bad+1)); }
fi

MD5_AFTER=$(md5_sources)
rc=0
echo
if [ "$MD5_BEFORE" != "$MD5_AFTER" ]; then
  echo "FAIL — the probe MODIFIED a real source file (md5 $MD5_BEFORE -> $MD5_AFTER)"; rc=1
else
  echo "tree unchanged: the real sources' md5 is identical before and after ($MD5_BEFORE)"
fi

# ================================================================================================================
# ★★★ THE VERDICT — EVERY TERM ENFORCED, NOT MERELY PRINTED.
# ================================================================================================================
echo "matrix: $n_cells configuration cells (pin $PIN_CELLS), $n_checks checks against the REAL mr_features.h, $n_fail failed (pin $PIN_CHECKS)"
[ "$matrix_rc" -eq 0 ]            || { echo "  !! a configuration cell failed to build or ran RED"; rc=1; }
[ "$envmap_rc" -eq 0 ]            || { echo "  !! the environment-map derivation FAILED (exit $envmap_rc)"; rc=1; }
[ "$own_rc" -eq 0 ]               || { echo "  !! the ownership contract FAILED or could not be evaluated (exit $own_rc)"; rc=1; }
[ "$n_fail" -eq 0 ]               || { echo "  !! $n_fail check(s) failed"; rc=1; }
[ "$n_cells" -eq "$PIN_CELLS" ]   || {
  echo "  !! CELL COUNT MOVED: $n_cells, pinned $PIN_CELLS — a configuration was added or dropped."
  echo "     If deliberate, update PIN_CELLS at the top of this file WITH its derivation."; rc=1; }
[ "$n_checks" -eq "$PIN_CHECKS" ] || {
  echo "  !! CHECK COUNT MOVED: $n_checks, pinned $PIN_CHECKS — a check was added or deleted."
  echo "     If deliberate, update PIN_CHECKS at the top of this file WITH its derivation."; rc=1; }

if [ "${1:-}" != "--no-neg" ]; then
  echo "controls: $n_ctl verified / $n_bad unusable (pin $PIN_CONTROLS)"
  [ "$n_bad" -eq 0 ]               || rc=1
  [ "$n_ctl" -eq "$PIN_CONTROLS" ] || {
    echo "  !! CONTROL COUNT MOVED: $n_ctl, pinned $PIN_CONTROLS — a control was added or dropped."
    echo "     If deliberate, update PIN_CONTROLS at the top of this file."; rc=1; }
  [ "$rc" -eq 0 ] && echo "PASS" || echo "FAIL"
else
  # ⛔ `--no-neg` MUST NEVER PRINT AN ORDINARY `PASS`. Without the controls this run cannot say the checks CAN
  #    fail, which is the sibling probes' documented trap.
  if [ "$rc" -eq 0 ]; then echo "PROBE-ONLY — NOT A GATE (controls skipped; run without --no-neg to gate)"
  else                     echo "FAIL"; fi
fi
exit $rc
