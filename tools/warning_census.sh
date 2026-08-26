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
# USAGE:  tools/warning_census.sh                 # gate every derived, pinned OLED env
#         tools/warning_census.sh --list          # show the derived OLED set and exit
#         EXPECT_heltec_v3=179 tools/…            # override one expectation (for a deliberate, reviewed change)
#
# ★ To RE-PIN after a legitimate change: run, read the reported actuals, and update BOTH the table here AND §B87 in
#   `docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md`. Two places on purpose — a silent re-pin is how a
#   pinned number stops being a gate.

set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

# ---- PINNED EXPECTATIONS (clean builds). A HIGHER count fails; a LOWER one also fails, because it means the baseline
#      moved and nobody re-pinned it. -Wswitch must be 0 everywhere, always. -------------------------------------
#
# ⛔ SUPERSEDED 2026-08-06 BY §B105 — the block immediately below is HISTORY, kept because it is the derivation the
#    new pin reverses. Its +2 was real and correctly attributed; B105 removed the cause. Read the §B105 block that
#    follows it for the LIVE numbers, and do not read "180/180/176" here as the current pin.
# ★★ RE-PINNED 2026-08-05 by §UI-6: 178/178/174 -> 180/180/176. **+2 PER ENV, AND EVERY ONE OF THEM IS A PER-TU
#    DIAGNOSTIC FROM A VENDORED HEADER — NOT ONE COMES FROM UI-6 CODE.** `src/firmware_ui.cpp` is the 326th object, and
#    it must reach `g_node` / `g_hal` / `g_iradio`, which means `src/fw_context.h`, which pulls `<RadioLib.h>` and
#    `lib/hal/device_radio.h`. Both emit once per INCLUDING TU:
#      +1  -Wcpp      RadioLib.h:58  `#warning "God mode active…"`   (5 -> 6 TUs)
#      +1  -Wvolatile device_radio.h `'++' of volatile-qualified type is deprecated`  (6 -> 7 TUs)
#    ⇒ ATTRIBUTED BY CONTROLLED A/B, not inferred: dropping `+<firmware_ui.cpp>` from `[env:heltec_v3]`'s
#    `build_src_filter` and rebuilding clean returns **325 objects / 178 warnings** exactly, and adds 6 `undefined
#    reference` errors for the three `mr_ui_*` hooks — which independently proves the TU is the hooks' only definition.
#    The `uniq -c` diff of the two logs is exactly the two lines above.
# ⚠ UI-6's OWN warnings were fixed, not pinned: the first build added **10** `-Wformat-truncation=` on top of these two,
#   and all ten are gone (the panel formatters' buffers are now sized to their provable widest expansion — see
#   `kLineCap` in src/firmware_ui.cpp, which says so, so nobody shrinks them back).
# ⓘ §B87's table in `docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md` IS NOW IN STEP (corrected 2026-08-05
#   under an explicit owner exception): 326 objects / 180 / 180 / 176, `-Wswitch` 0. ★ V1 2026-08-05 (§B115/§B117): the
#   RAM figures that used to be quoted here — 214116 / 213636 / 239036 — WENT STALE two slices ago and are now
#   **214396 / 213916 / 239316** (`gateway_heltec` = 73.03 %), re-measured to the byte in a controlled A/B. The
#   earlier note here — "the plan STILL SAYS 178/178/174" — was true when written and is no longer (V1).
# ★★★ RE-PINNED 2026-08-06 by §B105: 180/180/176 -> **178/178/174**. THIS IS B106 PLAYED BACKWARDS AND IT IS DECLARED,
#     NOT SILENT. `src/firmware_ui.cpp` no longer includes `fw_context.h`; it includes the new pure `fw_context_pure.h`
#     and reaches the radio through `DeviceHal::radio()` (an `IRadio&`, `lib/hal/device_hal.h`). The two per-TU
#     diagnostics the heavy include dragged in are therefore emitted by ONE FEWER TU each:
#       -1  -Wcpp      RadioLib.h  `#warning "God mode active…"`                    (6 -> 5 TUs)
#       -1  -Wvolatile device_radio.h `'++' of volatile-qualified type`             (7 -> 6 TUs)
#     ⇒ ATTRIBUTED BY CONTROLLED A/B, not inferred: two clean isolated `heltec_v3` builds, the second with
#     `#include "fw_context.h"` restored on the live file (then `md5`-verified restored). Totals 178 vs 180, and the
#     `uniq -c` diff of the two logs is EXACTLY the two lines above — nothing else moved.
# ⓘ Object count is UNCHANGED at 326 (no TU was added or removed) and `-Wswitch` stays 0.
# ⓘ RAM unchanged to the byte: 214396 / 213916 / 239316 (`gateway_heltec` = 73.03 %). Flash **+16 B on each** — the one
#   codegen delta, and it is expected: `mac_idle()` now dispatches `tx_busy()` virtually through `IRadio&` instead of
#   naming the concrete `g_iradio`. Same instance, same volatile contract, same predicate.
# ★ V4-3 ADDITION 2026-08-26: the new `heltec_v4` OLED environment measures 327 objects / 183 warnings /
#   `-Wswitch` 0. Its +5 versus `heltec_v3` is the existing RadioLib native-USB `#warning`, emitted once in each of
#   five translation units; `variants/heltec_v4/board_rf.cpp` accounts for the one additional object and adds no
#   warning. The existing 178/178/174 pins are unchanged.
declare -A EXPECT_WARN=( [heltec_v3]=178 [heltec_mobile]=178 [gateway_heltec]=174 [heltec_v4]=183 )

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
