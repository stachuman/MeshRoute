#!/usr/bin/env bash
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# §B95 console-sink probe — the ONLY automated cover `src/console_sink.h`'s line admission will ever have.
#
# WHY THIS EXISTS AND WHY IT IS NOT A `pio` ENV:
#   `src/` is compiled by NEITHER the native suite (`test_build_src = no`; there is no Arduino/Serial there) NOR the
#   simulator (which compiles `lib/core` only). So the behaviour B95 is about — WHICH BYTES REACH THE HOST, and in what
#   units — is unreachable by every automated gate this project has. ⇒ this probe host-compiles the REAL
#   `src/console_sink.h` against a transport model (`fakes/Arduino.h`) built from the two MEASURED ceilings:
#     • ESP32-S3 (heltec_v3 / heltec_mobile / xiao_esp32s3): `Serial` = UART0, no TX ring buffer ⇒
#       availableForWrite() == free bytes of the 128-B hardware FIFO.
#     • nRF52840 (xiao_sx1262 / gateway): TinyUSB CDC ⇒ 256 B, and its write() LOOPS WITH yield() if overfed.
#
# ★★ IT IS COMMITTED, DELIBERATELY, AND MUST STAY COMMITTED — same ruling as tools/probe_board_ui (owner, 2026-08-04).
#    A reconstruction recipe in a note is not a storage location; this project has already LOST a proven 33-assert
#    scenario to a session scratchpad.
#
# ★★ §0a/[[B208]] (2026-09-04) — THIS PROBE ALSO OWNS THE CONSOLE HELP. `src/firmware_help.h` was extracted so the
#    help text, the compact topic index and the whole `help`/`?`/`help <topic>` recognition could be COMPILED AND RUN
#    here instead of grepped. The run below builds the probe ONCE PER REAL PRODUCT PROFILE, renders every response
#    through the REAL GuardedConsole, and compares each profile's emitted CONTENT MULTISET against the frozen
#    pre-slice baseline (`help_baseline.json`) — which is what proves no help line was lost, duplicated or moved
#    behind a different gate by the split.
#
# USAGE:  tools/probe_console_sink/run.sh            # probe + NEGATIVE CONTROLS (the controls run BY DEFAULT)
#         tools/probe_console_sink/run.sh --no-neg   # probe only -- NOT a gate, use only while iterating
# ⚠ The controls run by default DELIBERATELY: a previous probe documented them as "not optional" while the standard
#   command skipped them, so the reported gate never included them (QA, 2026-08-04).
#
# WHAT IT PROVES, AND WHAT IT CANNOT:
#   • BEHAVIOURAL (the real header, compiled and run): every §7 test of the coder brief except 7 and 8.
#   • STRUCTURAL (grep, below): brief tests 7 and 8 plus invariant 9 — `dump_help`, `print_sf_list` and
#     `ble_dispatch_line` live in TUs that cannot be host-compiled (g_node, NV, JSON, the whole Arduino world), so
#     those three are asserted as SOURCE FACTS, each with its own negative control. Said plainly rather than dressed
#     up as a behavioural pass.

set -uo pipefail
cd "$(dirname "$0")" || exit 1
ROOT=$(cd ../.. && pwd)                 # ★ absolute — a relative path in a cwd-resetting shell silently measured
                                        #   nothing once already (register B82). Never make these relative.
HERE=$(pwd)
SINK="$ROOT/src/console_sink.h"
HELP="$ROOT/src/firmware_help.h"
BASELINE="$HERE/help_baseline.json"
CXX=${CXX:-g++}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
rc=0

# ⚠ These -D MUST mirror [common].build_flags + the MR_CONSOLE contract. If they drift, the probe measures a
#   configuration no board builds — the same vacuous-instrument failure the controls exist to catch.
FLAGS=(-std=gnu++2a -fno-exceptions -fno-rtti -Wall -Wextra -Werror -DARDUINO=100 -DMR_CONSOLE=1)
# ⚠ lib/core + lib/hal are on the path because `src/firmware_help.h` names its gates (mr_features.h,
#   protocol_constants.h) and the board RF-envelope text (rf_capabilities.h) EXPLICITLY rather than inheriting them
#   transitively. Neither directory holds a console_sink.h or a firmware_help.h, so neither can shadow a mutation.
INCS=(-I"$HERE/fakes" -I"$ROOT/src" -I"$ROOT/lib/core" -I"$ROOT/lib/hal")

# ★ THE REAL PRODUCT PROFILE MATRIX. Every row is the resolved macro set of at least one REAL board env
#   (`pio project config --json-output` + lib/core/mr_features.h); the env names are in help_manifest.py's PROFILES.
#   ⛔ `native` is deliberately absent: platformio.ini's `test_build_src = no` means no native target compiles src/.
PROFILES=(
  "full_oled|-DMR_FEAT_OLED=1"
  "full_headless|"
  "gateway|-DMR_N_LAYERS=2 -DMR_PROFILE_GATEWAY"
  "gateway_oled|-DMR_N_LAYERS=2 -DMR_PROFILE_GATEWAY -DMR_FEAT_OLED=1"
  "mobile|-DMR_PROFILE_MOBILE"
  "mobile_oled|-DMR_PROFILE_MOBILE -DMR_FEAT_OLED=1"
)

build() {   # build($1 = console_sink.h under test, $2 = firmware_help.h under test, $3 = out binary, $4 = profile, $5 = extra -D)
  local sdir hdir smd5 hmd5
  sdir=$(cd "$(dirname "$1")" && pwd); hdir=$(cd "$(dirname "$2")" && pwd)
  smd5=$(md5sum "$1" | cut -c1-8); hmd5=$(md5sum "$2" | cut -c1-8)
  # ★ -I the directory of EACH FILE UNDER TEST FIRST, before the repo's own src/: a stale copy in another include dir
  #   made a §UI-5 control pass spuriously. Both md5s are compiled IN and printed by the probe, so the output proves
  #   which text was measured — and tools/test_probe_console_sink.py re-derives them from the files.
  # shellcheck disable=SC2086
  "$CXX" -I"$sdir" -I"$hdir" "${FLAGS[@]}" "${INCS[@]}" $5 \
     -DPROBE_SINK_MD5="\"$smd5\"" -DPROBE_HELP_MD5="\"$hmd5\"" -DPROBE_PROFILE="\"$4\"" \
     "$HERE/probe_main.cpp" -o "$3" 2>&1
}

echo "== §B95 console-sink probe + §0a help router =="
echo "   sink md5 = $(md5sum "$SINK" | cut -c1-8)   help md5 = $(md5sum "$HELP" | cut -c1-8)   baseline md5 = $(md5sum "$BASELINE" | cut -c1-8)"
n_profiles=0
for row in "${PROFILES[@]}"; do
  prof=${row%%|*}; pflags=${row#*|}
  n_profiles=$((n_profiles + 1))
  if ! build "$SINK" "$HELP" "$OUT/probe_$prof" "$prof" "$pflags"; then
    echo "PROBE BUILD FAILED for profile $prof — see above"; exit 1
  fi
  "$OUT/probe_$prof" > "$OUT/run_$prof.txt" 2>&1; prc=$?
  if [ "$prof" = full_headless ]; then cat "$OUT/run_$prof.txt"; else
    sed -n '/§0a help router/,/HELP-CONTENT-BEGIN/p' "$OUT/run_$prof.txt" | sed '$d'
    grep -E '^  FAIL|passed / ' "$OUT/run_$prof.txt"
  fi
  echo "   profile $prof exit=$prc"
  [ "$prc" -eq 0 ] || rc=1
  # ---- the frozen-baseline content multiset, per profile (the no-line-lost proof) ----
  sed -n "/^HELP-CONTENT-BEGIN $prof/,/^HELP-CONTENT-END/p" "$OUT/run_$prof.txt" | sed '1d;$d' > "$OUT/content_$prof.txt"
  python3 "$HERE/help_manifest.py" compare "$BASELINE" "$OUT/content_$prof.txt" --profile "$prof" || rc=1
done
echo "   profiles measured: $n_profiles"

# ---- MR_CONSOLE=0 compile-out, MEASURED (brief §8: "prove Serial and staging compile out") -------------------------
echo
echo "== MR_CONSOLE=0 compile-out =="
cat > "$OUT/tu.cpp" <<'EOF'
#include <Arduino.h>
#include "console_sink.h"
FakeSerial Serial;
void touch() { mrcon.println("x"); mrcon.flush(); }
EOF
for mode in 1 0; do
  "$CXX" -std=gnu++2a -fno-exceptions -fno-rtti -Wall -Wextra -Werror -DARDUINO=100 -DMR_CONSOLE=$mode \
     -I"$HERE/fakes" -I"$(dirname "$SINK")" -c "$OUT/tu.cpp" -o "$OUT/tu$mode.o" || { echo "MR_CONSOLE=$mode BUILD FAILED"; rc=1; }
  # `nm -t d` — decimal radix. (mawk has no strtonum(); the first version of this line printed an empty size and
  #  then compared it, i.e. it would have "measured" nothing. Found by running it.)
  # Match the exact object symbol, not merely a line ending in `mrcon`: GCC also emits an 8-byte
  # `guard variable for mrcon`, and nm sorts that before the 2088-byte instance on this host.
  sz=$(nm -C -S -t d "$OUT/tu$mode.o" 2>/dev/null |
       awk '$3 ~ /^[uUBbCcDdGgSsVv]$/ && $4 == "mrcon" { print $2 + 0; exit }')
  refs=$(nm -C -u "$OUT/tu$mode.o" 2>/dev/null | grep -c 'FakeSerial' || true)
  echo "   MR_CONSOLE=$mode : sizeof(mrcon) = ${sz:-?} B   undefined Serial-type refs = $refs"
  if [ "$mode" = 0 ]; then
    [ "${sz:-0}" -le 16 ] || { echo "   !! MR_CONSOLE=0 STILL ALLOCATES ${sz} B — the staging storage did not compile out"; rc=1; }
  else
    [ "${sz:-0}" -ge 2048 ] || { echo "   !! MR_CONSOLE=1 mrcon is only ${sz} B — the stage is missing"; rc=1; }
  fi
done

# ---- STRUCTURAL checks: the two bypasses + the BLE help refusal ----------------------------------------------------
echo
echo "== structural checks (brief tests 7, 8 and invariant 9) =="
python3 "$HERE/structural.py" "$ROOT/src/firmware_commands.cpp" "$ROOT/src/firmware_commands.h" "$ROOT/src/fw_main.cpp" "$HELP" || rc=1

# ---- EXECUTED BLE help-refusal check (§0a owner ruling 2026-09-04: "help should not be transferred by BLE") -------
# The guard's condition is EXTRACTED from the real src/fw_main.cpp and compiled beside the real src/firmware_help.h,
# so the COMPOSITION (router owns X => BLE refuses X) is measured rather than argued. `fw_main.cpp` itself cannot be
# host-compiled, which is why the condition travels as text; the extraction is unique-or-refuse.
echo
echo "== BLE help-refusal (EXECUTED: the real guard condition x the real router) =="
python3 "$HERE/ble_guard.py" "$ROOT/src/fw_main.cpp" "$CXX" --out "$OUT" -- "${FLAGS[@]}" "${INCS[@]}" \
   > "$OUT/bleguard.txt" 2>&1 || rc=1
cat "$OUT/bleguard.txt"

if [ "${1:-}" = "--no-neg" ]; then
  # ⚠ VISIBLY PROBE-ONLY, AND IT NEVER PRINTS PASS. A previous probe documented its controls as "not optional" while
  #   the standard command skipped them, so the reported gate never included them (QA, 2026-08-04).
  echo
  echo "PROBE-ONLY (negative controls SKIPPED) — this is NOT a gate result. Re-run without --no-neg."
  exit $rc
fi

echo
echo "== negative controls (each MUST fail) =="
[ -f "$HERE/negctl.py" ] || { echo "negctl.py missing"; exit 1; }
# ★ Pass the paths AND the compiler config, so the controls cannot drift from the probe they are controlling.
python3 "$HERE/negctl.py" "$OUT" "$CXX" "$SINK" "$ROOT/src/firmware_commands.cpp" "$ROOT/src/firmware_commands.h" \
   "$ROOT/src/fw_main.cpp" "$HELP" "$BASELINE" -- "${FLAGS[@]}" "${INCS[@]}" > "$OUT/neg.txt" 2>&1 || rc=1
cat "$OUT/neg.txt"

# ---- DERIVED PINS. Counted from THIS run's own output, never typed. tools/test_probe_console_sink.py asserts them,
#      which is what stops a silent reduction (a dropped profile, a deleted control) from still reporting PASS.
chk_total=0
for row in "${PROFILES[@]}"; do
  prof=${row%%|*}
  n=$(sed -n 's/.*probe: \([0-9]*\) passed \/ \([0-9]*\) failed \/ \([0-9]*\) total.*/\3/p' "$OUT/run_$prof.txt")
  chk_total=$((chk_total + ${n:-0}))
done
struct_total=$(python3 "$HERE/structural.py" "$ROOT/src/firmware_commands.cpp" "$ROOT/src/firmware_commands.h" \
   "$ROOT/src/fw_main.cpp" "$HELP" | sed -n 's/.*structural: [0-9]* passed \/ [0-9]* failed \/ \([0-9]*\) total.*/\1/p')
ctl_total=$(sed -n 's/.*CONTROLS-TOTAL \([0-9]*\).*/\1/p' "$OUT/neg.txt")
ble_checks=$(sed -n 's/.*BLE-GUARD rows=[0-9]* checks=\([0-9]*\) failed=[0-9]*.*/\1/p' "$OUT/bleguard.txt")
green=$(grep -c 'STAYED GREEN\|INSTRUMENT FAILURE\|CONTROL NOT APPLIED' "$OUT/neg.txt" || true)
echo
echo "PINS profiles=${n_profiles} checks=${chk_total} structural=${struct_total} ble_guard=${ble_checks} controls=${ctl_total} unusable_controls=${green}"
[ "${green:-1}" -eq 0 ] || rc=1
if [ "$rc" -eq 0 ]; then
  echo "PASS: probe + structural + controls all green"
else
  echo "FAILED — see above"
fi
exit $rc
