#!/usr/bin/env bash
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# CLEAN-BUILD WARNING GATE for the OLED envs — §B87.
#
# ★★ WHY THIS EXISTS. "Warnings are gate-blocking" is a project rule (it caught three enum bugs), and §B87 pins a
# per-env warning total so a NEW warning still fails. ⚠ An INCREMENTAL `pio run` recompiles nothing and emits NO
# warnings, so the pin is unenforceable without a clean build — hence this script.
#
# ⚠⚠ REWRITTEN 2026-08-04 after QA proved v1 WAS NOT A GATE. It printed measurements and exited 0 — on a NONEXISTENT
# env it reported BUILD FAILED, zero objects and unknown sizes, then returned SUCCESS. An instrument that cannot fail
# measures nothing; that is the exact defect class this project keeps finding, and I shipped it in the file whose
# purpose is to prevent it. v1 also deleted the SHARED `.pio/build/<env>` dirs, which breaks the isolated-build rule and
# can collide with a concurrent agent.
#
# NOW: builds under a TEMPORARY `PLATFORMIO_BUILD_DIR` (shared state untouched), compares against the pinned
# expectations below, and EXITS NON-ZERO on any of: build failure · zero objects · warning-count mismatch · non-zero
# -Wswitch · a missing RAM/Flash metric.
#
# USAGE:  tools/warning_census.sh                 # gate the three pinned OLED envs
#         tools/warning_census.sh --list          # show the derived OLED set and exit
#         EXPECT_heltec_v3=179 tools/…            # override one expectation (for a deliberate, reviewed change)
#
# ★ To RE-PIN after a legitimate change: run, read the reported actuals, and update BOTH the table here AND §B87 in
#   `docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md`. Two places on purpose — a silent re-pin is how a
#   pinned number stops being a gate.

set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

# ---- PINNED EXPECTATIONS (clean builds, 2026-08-04). A HIGHER count fails; a LOWER one also fails, because it means
#      the baseline moved and nobody re-pinned it. -Wswitch must be 0 everywhere, always. -------------------------
declare -A EXPECT_WARN=( [heltec_v3]=178 [heltec_mobile]=178 [gateway_heltec]=174 )

# ⚠ `pio project config --environment <e>` emits NOTHING greppable for build_flags — a v2 derivation built on it
#   returned an empty set, and v1 only "worked" because I had passed the env names explicitly, so its
#   "derived from MR_FEAT_OLED" header was describing my TYPED LIST. One `pio project config` (no --environment)
#   emits every resolved section; awk over the `env:` headers is the form that actually derives.
derive_oled_envs() {
  pio project config 2>/dev/null | awk '/^env:/{e=substr($0,5)} /MR_FEAT_OLED=1/{print e}' | sort -u
}

if [ "${1:-}" = "--list" ]; then derive_oled_envs; exit 0; fi

# ⓘ The set is DERIVED, never typed: `gateway_heltec` was missed once by naming envs from memory instead of following
#   `extends` — the same sweep-scope error class this project keeps paying for. If derivation finds an env with no
#   pinned expectation, that is a FAILURE, not something to skip.
mapfile -t ENVS < <(derive_oled_envs)
[ "${#ENVS[@]}" -eq 0 ] && { echo "FAIL: no OLED envs derived"; exit 1; }

BUILD_DIR=$(mktemp -d)                      # ★ isolated: the shared .pio/build is never touched
trap 'rm -rf "$BUILD_DIR"' EXIT
rc_all=0

printf '%-18s %7s %9s %9s %9s %10s %10s  %s\n' env objs warn expect -Wswitch RAM Flash verdict
for e in "${ENVS[@]}"; do
  ov="EXPECT_$e"; exp="${!ov:-${EXPECT_WARN[$e]:-}}"
  log=$(mktemp)
  PLATFORMIO_BUILD_DIR="$BUILD_DIR" pio run -e "$e" > "$log" 2>&1; brc=$?
  objs=$(find "$BUILD_DIR/$e" -name '*.o' 2>/dev/null | wc -l)
  warn=$(grep -c 'warning:' "$log")
  wsw=$(grep -c 'Wswitch' "$log")
  ram=$(grep -oE 'RAM:.*\(used [0-9]+' "$log" | grep -oE '[0-9]+$' | tail -1)
  flash=$(grep -oE 'Flash:.*\(used [0-9]+' "$log" | grep -oE '[0-9]+$' | tail -1)

  why=()
  [ "$brc" -ne 0 ]                  && why+=("BUILD FAILED")
  [ "$objs" -eq 0 ]                 && why+=("ZERO OBJECTS (measured nothing)")
  [ -z "$exp" ]                     && why+=("NO PINNED EXPECTATION for this env")
  [ -n "$exp" ] && [ "$warn" -ne "$exp" ] && why+=("warnings $warn != pinned $exp")
  [ "$wsw" -ne 0 ]                  && why+=("-Wswitch $wsw != 0")
  [ -z "${ram:-}" ] || [ -z "${flash:-}" ] && why+=("missing RAM/Flash metric")

  if [ "${#why[@]}" -eq 0 ]; then verdict=ok; else verdict="FAIL: ${why[*]}"; rc_all=1; fi
  printf '%-18s %7s %9s %9s %9s %10s %10s  %s\n' \
         "$e" "$objs" "$warn" "${exp:-–}" "$wsw" "${ram:-?}" "${flash:-?}" "$verdict"
  [ "$brc" -ne 0 ] && grep -m3 -E 'error:|Error' "$log" | sed 's/^/     /'
  rm -f "$log"
done

echo
if [ "$rc_all" -eq 0 ]; then echo "PASS — ${#ENVS[@]} OLED env(s) match their pinned warning baseline"
else echo "FAIL — see rows above; re-pin ONLY via a reviewed change to this table AND §B87"; fi
exit "$rc_all"
