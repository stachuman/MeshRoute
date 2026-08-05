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
CXX=${CXX:-g++}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
rc=0

# ⚠ These -D MUST mirror [common].build_flags + the MR_CONSOLE contract. If they drift, the probe measures a
#   configuration no board builds — the same vacuous-instrument failure the controls exist to catch.
FLAGS=(-std=gnu++2a -fno-exceptions -fno-rtti -Wall -Wextra -Werror -DARDUINO=100 -DMR_CONSOLE=1)

build() {   # build($1 = console_sink.h to compile against, $2 = output binary)
  local dir; dir=$(cd "$(dirname "$1")" && pwd)
  local md5; md5=$(md5sum "$1" | cut -c1-8)
  # ★ -I the directory of THE FILE UNDER TEST FIRST and nothing else that could hold a console_sink.h: a stale copy
  #   in another include dir made a §UI-5 control pass spuriously. The md5 is compiled IN and printed by the probe,
  #   so the output proves which text was measured.
  "$CXX" "${FLAGS[@]}" -DPROBE_SINK_MD5="\"$md5\"" \
     -I"$HERE/fakes" -I"$dir" "$HERE/probe_main.cpp" -o "$2" 2>&1
}

echo "== §B95 console-sink probe =="
if ! build "$SINK" "$OUT/probe"; then
  echo "PROBE BUILD FAILED — see above"; exit 1
fi
"$OUT/probe"; prc=$?
echo "probe exit=$prc"
[ "$prc" -eq 0 ] || rc=1

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
python3 "$HERE/structural.py" "$ROOT/src/firmware_commands.cpp" "$ROOT/src/firmware_commands.h" "$ROOT/src/fw_main.cpp" || rc=1

if [ "${1:-}" != "--no-neg" ]; then
  echo
  echo "== negative controls (each MUST fail) =="
  [ -f "$HERE/negctl.py" ] || { echo "negctl.py missing"; exit 1; }
  # ★ Pass the paths AND the compiler config, so the controls cannot drift from the probe they are controlling.
  python3 "$HERE/negctl.py" "$OUT" "$CXX" "$SINK" "$ROOT/src/firmware_commands.cpp" "$ROOT/src/firmware_commands.h" \
     "$ROOT/src/fw_main.cpp" -- "${FLAGS[@]}" -I"$HERE/fakes" || rc=1
fi
exit $rc
