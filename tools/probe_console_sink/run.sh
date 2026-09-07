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
# ★★ §0a/[[B208]] + §0g (2026-09-05) — THIS PROBE ALSO OWNS THE CONSOLE HELP. `src/firmware_help.h` was extracted
#    so the whole `help`/`?` recognition could be COMPILED AND RUN here instead of grepped. The run below builds the
#    probe ONCE PER REAL PRODUCT PROFILE, renders every response through the REAL GuardedConsole, and compares each
#    profile's emitted NAME LIST against `tools/gen_command_inventory.py --primary <profile>`.
# ★★ THE ORACLE CHANGED IN §0g, AND THAT IS THE POINT ([[B291]]). It used to be a FROZEN pre-slice content multiset
#    (`help_baseline.json` + `help_manifest.py`), which could only ever say "nothing moved since 0a": any legitimate
#    help edit reddened the sweep, and the only documented way to regenerate it was `git show` of a deleted function.
#    The owner then removed all descriptive text, so there is no content left to freeze. The oracle is now the
#    GENERATED COMMAND INVENTORY — derived from the real dispatchers on the CURRENT tree — so the gate compares
#    production output against SOURCE in both directions, and adding a command updates the expectation by itself.
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
GEN="$ROOT/tools/gen_command_inventory.py"      # §0g: the primary-verb ORACLE (scans the real dispatchers)
MANUAL_POINTER="docs/manual/command-reference.md"   # the ONE non-command line of every help response
CXX=${CXX:-g++}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
rc=0

# ⚠ These -D MUST mirror [common].build_flags + the MR_CONSOLE contract. If they drift, the probe measures a
#   configuration no board builds — the same vacuous-instrument failure the controls exist to catch.
FLAGS=(-std=gnu++2a -fno-exceptions -fno-rtti -Wall -Wextra -Werror -DARDUINO=100 -DMR_CONSOLE=1)
# ⚠ lib/core + lib/hal are on the path because `src/firmware_help.h` names its gates (mr_features.h,
#   protocol_constants.h) EXPLICITLY rather than inheriting them transitively. §0g dropped the header's
#   rf_capabilities.h dependency with the cfg envelope text it fed; lib/hal stays on the path for the sink's own
#   includes. Neither directory holds a console_sink.h or a firmware_help.h, so neither can shadow a mutation.
# §RADMIN slice 3 added lib/console + lib/monocypher/src: `ble_guard.py`'s ADMIN family compiles the REAL
# `src/firmware_admin_verbs.h`, which reaches `remote_codec.h` (the ten-slot binding) and `monocypher.h`
# (R-RA-29's BLAKE2b). Neither directory holds a console_sink.h or a firmware_help.h, so neither can shadow
# a mutation — the same argument the two lines above make for lib/core and lib/hal.
INCS=(-I"$HERE/fakes" -I"$ROOT/src" -I"$ROOT/lib/core" -I"$ROOT/lib/hal" -I"$ROOT/lib/console" -I"$ROOT/lib/monocypher/src")

# ★ THE REAL PRODUCT PROFILE MATRIX. Every row is the resolved macro set of at least one REAL board env
#   (`pio project config --json-output` + lib/core/mr_features.h); the macro sets and their env names now live in
#   `tools/gen_command_inventory.py`'s PROFILES/PROFILE_ENVS, which is also what projects the expected name list.
#   ⛔ `native` is deliberately absent: platformio.ini's `test_build_src = no` means no native target compiles src/.
PROFILES=(
  "full_oled|-DMR_FEAT_OLED=1"
  "full_headless|"
  "gateway|-DMR_N_LAYERS=2 -DMR_PROFILE_GATEWAY"
  "gateway_oled|-DMR_N_LAYERS=2 -DMR_PROFILE_GATEWAY -DMR_FEAT_OLED=1"
  "mobile|-DMR_PROFILE_MOBILE"
  "mobile_oled|-DMR_PROFILE_MOBILE -DMR_FEAT_OLED=1"
)

# ★★ [[B294]] THE RUNNER'S OWN PINS — DERIVED, WRITTEN DOWN, AND ENFORCED **HERE**, BEFORE PASS.
#   Until slice 0g these six numbers were PRINTED and never COMPARED in this file: only
#   `tools/test_probe_console_sink.py` checked them, so running the gate directly could print PASS after coverage
#   silently shrank (a dropped profile, a deleted control, a check count that fell). The wrapper REMAINS, as an
#   independent second reader; this block is the runner judging itself.
#   ⛔ NO ENVIRONMENT VARIABLE OR FLAG MAY RAISE OR LOWER A PIN IN A NORMAL RUN. The selftest hook below can only
#     REDUCE what is measured, which can only ever produce a FAILURE.
#   THE ARITHMETIC:
#     PIN_PROFILES   = 6                     the six real board macro sets in the matrix above.
#     PIN_CHECKS     = CHECKS_PER_PROFILE * PIN_PROFILES = 120 * 6 = 720
#                                            the summed `N total` the probe binary prints. 120 = 52 §B95 sink rows
#                                            + 68 §0g help rows: H0 1, H1 4, H2 6, H3 6 (one per GATED name), H4 6,
#                                            H5 20 (nine retired topic words + eleven malformed tails), H6 8,
#                                            H7 16 (four commands x four wire assertions), H8 1. Every profile runs
#                                            the SAME rows — a gated name changes the LIST, not the row count —
#                                            which is why one per-profile constant is enough. (§0a was 166/160: the
#                                            per-topic rows scaled with topic availability.)
#     PIN_STRUCTURAL = 52                    structural.py's S1..S52. ⚠ RE-PINNED 2026-09-07 BY §RADMIN
#         SLICE 5: +2 (S51, S52) — the ACCEPT bindings acquire a RULED `Node` link (design §6.5's live activation,
#         R-RA-31), so S34's blanket "touch NO Node state" was replaced IN PLACE by the narrower legacy-symbol
#         rule plus S51, which pins the Node surface to EXACTLY the three session entry points. ⛔ The count
#         moved because checks were ADDED, not because one was weakened: S34 still forbids every legacy
#         single-admin symbol, S51 refuses any fourth unreviewed `g_node.` call inside those blocks, and S52
#         pins that both entry points actually BIND the seam (dropping the argument would leave every emitted
#         byte identical while silently reverting the slice to Slice 3's install-nothing behaviour).
#     (historical:) PIN_STRUCTURAL = 50       structural.py's S1..S50. ⚠ RE-PINNED 2026-09-06 BY §RADMIN
#         SLICE 4, 39 -> 50, FULLY ATTRIBUTED: eleven NEW device-boundary rows for the two CONTROLLER
#         stores — S40 the CLIENT boot report is called exactly once from setup() under
#         MR_FEAT_RADMIN_CLIENT · S41 and it runs AFTER the mount/self-heal · S42 that path writes
#         nothing and draws nothing · S43 ★ EXACTLY ONE resident controller buffer exists and it is the
#         PUBLIC book (a COUNT, not an absence — design §6.2 rules one) · S44 ★★ ⛔ NO resident keyring,
#         service or seed (the SECRETS stay stack transients) · S45 the CLIENT bindings touch no Node
#         state · S46 ★★ the typed wrappers address kSlotMgmtKeys / kSlotTargets and NEITHER touches
#         kSlotAdmid (design §6.4's no-crossing rule, as source) · S47 neither record joins
#         mount_or_repair()'s probe list ([[B317]]) · S48 do_regen()'s write set is STILL {/mrid}, which
#         is what makes the `keys and targets preserved` warning a proven claim · S49 handle_leave()'s is
#         still {/mrcfg} · S50 the CLIENT BLE refusal carries its OWN `admin-client` envelope, distinct
#         from the target family's. ⛔ NOT ONE of S1..S39 moved; each new row has a sabotage control.
#         (historical:)  PIN_STRUCTURAL = 39 — structural.py's S1..S39. RE-PINNED 2026-09-06 BY §RADMIN
#         SLICE 3, 29 -> 39, FULLY ATTRIBUTED: ten NEW device-boundary rows for the two target stores —
#         S30 the boot report is called exactly once from setup() under MR_FEAT_RADMIN_ACCEPT · S31 and it
#         runs AFTER the filesystem mount/self-heal · S32 the boot path writes nothing and draws nothing ·
#         S33 no resident identity/ACL/service/static record buffer (design §6.2) · S34 the ACCEPT bindings
#         touch no Node state and no legacy single-admin symbol · S35 the typed wrappers address kSlotAdmid /
#         kSlotAcl and nothing else · S36 mount_or_repair()'s probe list is still the SAME SIX files
#         ([[B317]]) · S37 do_regen()'s write set is still {/mrid} · S38 handle_leave()'s is still {/mrcfg} ·
#         S39 factory_erase() still erases WHOLESALE, so both new records are covered with zero new code.
#         ⛔ NOT ONE of S1..S29 moved. Each new row has a deliberate sabotage control in negctl.py.
#         (historical:)  structural.py's S1..S29. §0b/[[B279]] added S21 (fw_main.cpp passes
#                                            the boot identity formatter its sink explicitly, exactly once);
#                                            §RADMIN-0c added EIGHT: S22/S23 the two one-call `fw_main.cpp`
#                                            adapters (one seam call each, no residual router/parser/Node fork,
#                                            their own sinks, exactly one seam flush), S24 the seam's fork happens
#                                            once and ROUTER-FIRST, S25 the seam never re-chooses a global sink,
#                                            S26 no borrowed body and no static state, S27 the send handle is still
#                                            HEX (the probes' Arduino fake ignores a radix, so nothing EXECUTED can
#                                            see this), S28/S29 the [[B298]] comment census in the .cpp and the .h.
#     PIN_BLE_GUARD  = 905                   ⚠ RE-PINNED 2026-09-06 BY §RADMIN SLICE 4, 480 -> 905, DERIVED:
#           help   : 53 corpus lines x 4 assertions (B1..B4) = 212   (UNCHANGED)
#           admin  : 67 corpus lines x 4 assertions (A1..A4) = 268   (UNCHANGED)
#           client : 85 corpus lines x 5 assertions (D1..D5) = 425   (NEW — R-RA-30's SUB-VERB split needs a
#                    FIFTH assertion the whole-family families do not: D4, "a RULED PUBLIC form must NOT be
#                    refused". A family that only ever refuses cannot express that, which is exactly why the
#                    extraction is generalized per family rather than copied.)
#           212 + 268 + 425 = 905. ✓  85 = 2 families x (12 public + 20 secret tails) + 21 foreign lines.
#         (historical:)  PIN_BLE_GUARD = 480 — RE-PINNED 2026-09-06 BY §RADMIN SLICE 3, 212 -> 480, and the
#         extractor is GENERALIZED rather than copied: it now answers the same question once per FAMILY.
#           help  : 53 corpus lines x 4 assertions (B1..B4) = 212   (UNCHANGED — every prior row still runs)
#           admin : 67 corpus lines x 4 assertions (A1..A4) = 268   (R-RA-29)
#         212 + 268 = 480. ✓  The admin corpus is 2 families x 22 subforms = 44 owned rows (malformed subforms
#         INCLUDED) + 23 near misses and foreign tokens, and its `must_refuse` column is the OWNER'S RULE,
#         never read off the predicate. The admin extraction additionally REFUSES unless the site is inside
#         `ble_dispatch_line`, BEFORE `exec_console_line`, and gated on EXACTLY `MR_FEAT_RADMIN_ACCEPT`.
#     PIN_OWNERSHIP  = 6                     ownership.py: one row per REAL product profile, each requiring the
#                                            router-owned and parser-owned primary-form sets to be DISJOINT,
#                                            non-empty, complete against the generator's own projection, and at
#                                            their derived counts. This is the authority for the seam's one order.
#     PIN_OWN_CTL    = 3                     ownership.py --selftest: a synthetic collision REFUSED, a deleted
#                                            router form REFUSED, an emptied parser surface REFUSED.
#     PIN_CONTROLS   = 99 = 8 sink + 22 source + 23 help + 5 BLE + 20 radmin3 + 21 radmin4 (negctl's own
#         CONTROLS-TOTAL). ⚠ RE-PINNED 2026-09-06 BY §RADMIN SLICE 4, 78 -> 99, DERIVED: the 21 new ones are
#         5 EXECUTED guard controls (D-C1 the guard refuses nothing · D-C2 ★ it refuses the RULED PUBLIC
#         list/show too — the TOO-WIDE direction a whole-family guard cannot even express · D-C3 only half the
#         family is guarded · D-C4 the sub-verb test becomes a prefix · D-C5 a broad `admin` prefix swallows the
#         TARGET half) + 6 EXTRACTION-refusal controls (D-C6..D-C11: deleted · duplicated · commented out ·
#         ★ the envelope COLLIDES with the target family's · moved below the seam · gated on the wrong
#         capability) + 10 structural sabotages (S-C40 · S-C40b · S-C42 · S-C43 · S-C44 · S-C45 · S-C46 ·
#         S-C47 · S-C48 · S-C50). 5 + 6 + 10 = 21, and 78 + 21 = 99. ✓
#         (historical:)  PIN_CONTROLS = 78 = 8 sink + 22 source + 23 help + 5 BLE + 20 radmin3;
#                      §0b/[[B279]] added ONE source control (X12 -> S21) and §RADMIN-0c added TEN (X13..X22 ->
#                      S22..S29).                the 23 help = 13 rendered-index mutations + 2 structural + 3 router
#                                            + 5 oracle.
#         ⚠ RE-PINNED 2026-09-06 BY §RADMIN SLICE 3, 58 -> 78, FULLY ATTRIBUTED and with ⛔ not one prior control
#           dropped. The 20 are: THREE executed-row sabotages of the ADMIN BLE guard (A-C1 partial family, A-C2
#           listing-only escape, A-C3 broad `admin` prefix that would swallow Slice 4's `admin-key`) · SIX
#           extraction REFUSALS (A-C4 deleted, A-C5 duplicated, A-C6 comment-only, A-C7 wrong envelope, A-C8 moved
#           BELOW the transport seam, A-C9 gated on the WRONG capability) · ELEVEN device-boundary sabotages, one
#           per structural claim (S-C30 duplicated boot call, S-C30b the boot call loses its gate, S-C31 the boot
#           report hoisted above the mount, S-C32 the boot path starts writing, S-C33 a RESIDENT service appears,
#           S-C34 the bindings reach into Node, S-C35 load_acl re-pointed at the other slot, S-C36 /mradmid added
#           to the self-heal probe list, S-C37 do_regen starts writing the ACL, S-C38 handle_leave starts writing
#           /mradmid, S-C39 factory_erase stops erasing wholesale). 3 + 6 + 11 = 20. ✓
#         ⚠ RE-PINNED 2026-09-07 BY §RADMIN SLICE 5, 99 -> 101, and ⛔ not one prior control dropped. The 2 are
#           S-C34b (the ACL service loses its live-install seam) and S-C34c (the IDENTITY service loses it), both
#           checked by the new S52. ★ S-C34 itself was RE-POINTED from S34 to S51 rather than added or removed:
#           its injected defect (an unreviewed `g_node.` reach inside an ACCEPT binding) is unchanged, but the
#           rule that catches it moved when S34 legitimately stopped forbidding every Node contact. Left
#           unchanged it STAYED GREEN — the runner said so and refused to score it, which is what forced this
#           re-pointing instead of a silent pass.
PIN_PROFILES=6
CHECKS_PER_PROFILE=120
PIN_CHECKS=$((CHECKS_PER_PROFILE * PIN_PROFILES))
PIN_STRUCTURAL=52
PIN_BLE_GUARD=905
PIN_OWNERSHIP=6
PIN_OWN_CTL=3
PIN_CONTROLS=101

pin_fail=0
pin_cmp() {   # pin_cmp <term> <observed> <expected> — a missing, non-numeric, zero or differing count is a FAILURE
  case "${2:-}" in
    '')        echo "   !! PIN $1: MISSING — this run printed no value for it"; pin_fail=1; return 0 ;;
    *[!0-9]*)  echo "   !! PIN $1: NON-NUMERIC observed value '$2'";            pin_fail=1; return 0 ;;
  esac
  if [ "$2" -eq 0 ] && [ "$3" -ne 0 ]; then
    echo "   !! PIN $1: ZERO — nothing was measured (expected $3)"; pin_fail=1; return 0
  fi
  if [ "$2" -ne "$3" ]; then
    echo "   !! PIN $1: observed $2, expected $3"; pin_fail=1
  fi
}

# ⚠ THE SELFTEST HOOK. `MRPROBE_SELFTEST_DROP` removes ONE profile from the matrix so the pin comparator can be
#   PROVEN to notice shrinking coverage while every probe binary it launched stays green — the exact hole B294
#   describes. It only ever REDUCES coverage, so it cannot manufacture a PASS: the pins stay at their production
#   values and refuse. It is exercised by `--pin-selftest` below and by tools/test_probe_console_sink.py.
if [ -n "${MRPROBE_SELFTEST_DROP:-}" ]; then
  _keep=()
  for row in "${PROFILES[@]}"; do [ "${row%%|*}" = "$MRPROBE_SELFTEST_DROP" ] || _keep+=("$row"); done
  PROFILES=("${_keep[@]}")
  echo "!! SELFTEST INJECTION: profile '$MRPROBE_SELFTEST_DROP' dropped — coverage deliberately reduced"
fi

if [ "${1:-}" = "--pin-selftest" ]; then
  # ⛔ NOT A GATE RESULT, AND IT NEVER PRINTS THE GATE'S PASS WORDING. It proves the pin comparator refuses.
  echo "== §0g PIN SELFTEST ([[B294]]) — every pin class must refuse =="
  st_fail=0
  # (a) the comparator itself: the correct value passes; MISSING, NON-NUMERIC, ZERO and OFF-BY-ONE are all caught.
  while IFS='|' read -r obs exp want; do
    pin_fail=0
    pin_cmp "selftest" "$obs" "$exp" > /dev/null 2>&1
    got=ok; [ "$pin_fail" -eq 0 ] || got=refused
    if [ "$got" = "$want" ]; then
      echo "   ok   comparator observed='$obs' expected=$exp -> $got"
    else
      echo "   !! SELFTEST HOLE: comparator observed='$obs' expected=$exp -> $got, wanted $want"; st_fail=1
    fi
  done <<'CASES'
10|10|ok
9|10|refused
|10|refused
abc|10|refused
0|10|refused
CASES
  # (b) END TO END, THE F4 SHAPE: drop one profile. Every probe binary still prints "0 failed", but the observed
  #     profile and check counts shrink — and the runner itself must refuse to print PASS.
  child=$(mktemp)
  # ⚠ "$HERE/run.sh", never "$0": this script cd's to its own directory on line 2, so a relative $0 no longer
  #   resolves by the time we get here — the child would fail to launch and the selftest would misread that as a
  #   missing pin refusal.
  MRPROBE_SELFTEST_DROP=mobile_oled bash "$HERE/run.sh" > "$child" 2>&1; crc=$?
  greens=$(grep -c 'passed / 0 failed' "$child" || true)
  if [ "$crc" -eq 0 ]; then
    echo "   !! SELFTEST HOLE: the runner exited 0 with a profile dropped"; st_fail=1
  elif grep -q 'PASS: probe + structural + controls all green' "$child"; then
    echo "   !! SELFTEST HOLE: the runner printed its PASS wording with a profile dropped"; st_fail=1
  elif ! grep -q '!! PIN profiles:' "$child"; then
    echo "   !! SELFTEST HOLE: no 'PIN profiles' refusal was printed"; st_fail=1
  elif ! grep -q '!! PIN checks:' "$child"; then
    echo "   !! SELFTEST HOLE: no 'PIN checks' refusal was printed"; st_fail=1
  elif [ "${greens:-0}" -lt 1 ]; then
    echo "   !! SELFTEST HOLE: no probe binary stayed green, so this proves nothing about the F4 shape"; st_fail=1
  else
    echo "   ok   dropped profile: $greens probe binaries STILL GREEN, runner refused (exit $crc) on profiles+checks"
  fi
  rm -f "$child"
  if [ "$st_fail" -eq 0 ]; then echo "SELFTEST OK — every pin class refuses"; else echo "SELFTEST FAILED"; fi
  exit $st_fail
fi

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

echo "== §B95 console-sink probe + §0g bare help index =="
echo "   sink md5 = $(md5sum "$SINK" | cut -c1-8)   help md5 = $(md5sum "$HELP" | cut -c1-8)   inventory oracle = $GEN"
n_profiles=0
for row in "${PROFILES[@]}"; do
  prof=${row%%|*}; pflags=${row#*|}
  n_profiles=$((n_profiles + 1))
  if ! build "$SINK" "$HELP" "$OUT/probe_$prof" "$prof" "$pflags"; then
    echo "PROBE BUILD FAILED for profile $prof — see above"; exit 1
  fi
  "$OUT/probe_$prof" > "$OUT/run_$prof.txt" 2>&1; prc=$?
  if [ "$prof" = full_headless ]; then cat "$OUT/run_$prof.txt"; else
    sed -n '/§0g help index/,/HELP-NAMES-BEGIN/p' "$OUT/run_$prof.txt" | sed '$d'
    grep -E '^  FAIL|passed / ' "$OUT/run_$prof.txt"
  fi
  echo "   profile $prof exit=$prc"
  [ "$prc" -eq 0 ] || rc=1
  # ---- §0g: production output vs the GENERATED INVENTORY, both directions, per profile --------------------------
  # The probe printed the index VERBATIM. Strip exactly the trailing manual pointer, then diff the remainder against
  # the projection the generator derived by scanning the real dispatchers. ⛔ Both sides must be non-empty: an empty
  # file compares clean against another empty file, which is the vacuous pass this whole block exists to refuse.
  sed -n "/^HELP-NAMES-BEGIN $prof/,/^HELP-NAMES-END/p" "$OUT/run_$prof.txt" | sed '1d;$d' > "$OUT/idx_$prof.txt"
  if [ "$(tail -n 1 "$OUT/idx_$prof.txt")" != "$MANUAL_POINTER" ]; then
    echo "   !! $prof: the index does not END with the manual pointer ($MANUAL_POINTER)"; rc=1
  fi
  head -n -1 "$OUT/idx_$prof.txt" > "$OUT/got_$prof.txt"
  if ! python3 "$GEN" --primary "$prof" > "$OUT/want_$prof.txt"; then
    echo "   !! $prof: the inventory projection REFUSED — no oracle, so no result"; rc=1
  fi
  n_got=$(wc -l < "$OUT/got_$prof.txt"); n_want=$(wc -l < "$OUT/want_$prof.txt")
  if [ "$n_got" -lt 1 ] || [ "$n_want" -lt 1 ]; then
    echo "   !! $prof: EMPTY side (rendered=$n_got projected=$n_want) — refusing a vacuous comparison"; rc=1
  elif diff -u "$OUT/want_$prof.txt" "$OUT/got_$prof.txt" > "$OUT/diff_$prof.txt"; then
    echo "   primary names $prof: $n_got rendered == $n_want projected (inventory oracle)"
  else
    echo "   !! $prof: the rendered help names DIFFER from the source projection (-want +got):"
    sed -n '4,24p' "$OUT/diff_$prof.txt"; rc=1
  fi
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
python3 "$HERE/structural.py" "$ROOT/src/firmware_commands.cpp" "$ROOT/src/firmware_commands.h" "$ROOT/src/fw_main.cpp" "$HELP" \
   "$ROOT/src/device_nv.h" "$ROOT/src/firmware_config.cpp" || rc=1

# ---- EXECUTED BLE help-refusal check (§0a owner ruling 2026-09-04: "help should not be transferred by BLE") -------
# The guard's condition is EXTRACTED from the real src/fw_main.cpp and compiled beside the real src/firmware_help.h,
# so the COMPOSITION (router owns X => BLE refuses X) is measured rather than argued. `fw_main.cpp` itself cannot be
# host-compiled, which is why the condition travels as text; the extraction is unique-or-refuse.
echo
echo "== BLE help-refusal (EXECUTED: the real guard condition x the real router) + the §RADMIN-3 admin family =="
python3 "$HERE/ble_guard.py" "$ROOT/src/fw_main.cpp" "$CXX" --out "$OUT" -- "${FLAGS[@]}" "${INCS[@]}" \
   > "$OUT/bleguard.txt" 2>&1 || rc=1
cat "$OUT/bleguard.txt"

# ---- §RADMIN-0c: THE ROUTER/PARSER OWNERSHIP GATE (the seam's licence to have ONE order) -------------------------
# The 0c seam asks the console router first and the command parser second, on BOTH transports. That unification is
# behaviour-preserving ONLY while the two surfaces are disjoint — on a line both accept, the order IS the behaviour.
# This derives both sets from the GENERATED inventory on every real product profile and refuses a collision; its own
# controls prove it refuses a synthetic one.
echo
echo "== router/parser ownership (derived from the generated inventory, all six real profiles) =="
python3 "$HERE/ownership.py" > "$OUT/ownership.txt" 2>&1 || rc=1
cat "$OUT/ownership.txt"

if [ "${1:-}" = "--no-neg" ]; then
  # ⚠ VISIBLY PROBE-ONLY, AND IT NEVER PRINTS PASS. A previous probe documented its controls as "not optional" while
  #   the standard command skipped them, so the reported gate never included them (QA, 2026-08-04).
  echo
  echo "PROBE-ONLY (negative controls SKIPPED) — this is NOT a gate result. Re-run without --no-neg."
  exit $rc
fi

echo
echo "== router/parser ownership controls (each MUST refuse) =="
python3 "$HERE/ownership.py" --selftest > "$OUT/ownctl.txt" 2>&1 || rc=1
sed 's/^/  /' "$OUT/ownctl.txt"

echo
echo "== negative controls (each MUST fail) =="
[ -f "$HERE/negctl.py" ] || { echo "negctl.py missing"; exit 1; }
# ★ Pass the paths AND the compiler config, so the controls cannot drift from the probe they are controlling.
python3 "$HERE/negctl.py" "$OUT" "$CXX" "$SINK" "$ROOT/src/firmware_commands.cpp" "$ROOT/src/firmware_commands.h" \
   "$ROOT/src/fw_main.cpp" "$HELP" -- "${FLAGS[@]}" "${INCS[@]}" > "$OUT/neg.txt" 2>&1 || rc=1
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
   "$ROOT/src/fw_main.cpp" "$HELP" "$ROOT/src/device_nv.h" "$ROOT/src/firmware_config.cpp" | sed -n 's/.*structural: [0-9]* passed \/ [0-9]* failed \/ \([0-9]*\) total.*/\1/p')
ctl_total=$(sed -n 's/.*CONTROLS-TOTAL \([0-9]*\).*/\1/p' "$OUT/neg.txt")
# §RADMIN slice 3: TWO families now, so the total is their SUM. ⛔ `sed -n …p` prints one line per family and
#   a bare assignment would have kept only the LAST — i.e. silently dropped the help family's 212 rows.
# §RADMIN slice 4: THREE families. ⚠ THE SUM IS ALSO A COVERAGE PIN: a family whose extraction REFUSED prints no
#   count line at all, so the total FALLS and `pin_cmp` reddens — which is the whole reason this is a sum and not
#   a per-family "did it fail" test.
ble_checks=$( { sed -n 's/.*BLE-GUARD rows=[0-9]* checks=\([0-9]*\) failed=[0-9]*.*/\1/p' "$OUT/bleguard.txt";
                sed -n 's/.*ADMIN-GUARD rows=[0-9]* checks=\([0-9]*\) failed=[0-9]*.*/\1/p' "$OUT/bleguard.txt";
                sed -n 's/.*CLIENT-GUARD rows=[0-9]* checks=\([0-9]*\) failed=[0-9]*.*/\1/p' "$OUT/bleguard.txt";
              } | awk '{t+=$1} END {print t+0}')
green=$(grep -c 'STAYED GREEN\|INSTRUMENT FAILURE\|CONTROL NOT APPLIED' "$OUT/neg.txt" || true)
own_total=$(sed -n 's/.*ownership: [0-9]* passed \/ [0-9]* failed \/ \([0-9]*\) total.*/\1/p' "$OUT/ownership.txt")
own_ctl=$(grep -c '^  ok   C-' "$OUT/ownctl.txt" || true)
echo
echo "PINS profiles=${n_profiles} checks=${chk_total} structural=${struct_total} ble_guard=${ble_checks} ownership=${own_total} ownership_controls=${own_ctl} controls=${ctl_total} unusable_controls=${green}"
# ---- [[B294]]: THE RUNNER'S OWN JUDGMENT, BEFORE PASS. Every term is compared; the differing one is NAMED. -------
pin_cmp profiles    "${n_profiles:-}"  "$PIN_PROFILES"
pin_cmp checks      "${chk_total:-}"   "$PIN_CHECKS"
pin_cmp structural  "${struct_total:-}" "$PIN_STRUCTURAL"
pin_cmp ble_guard   "${ble_checks:-}"  "$PIN_BLE_GUARD"
pin_cmp ownership   "${own_total:-}"   "$PIN_OWNERSHIP"
pin_cmp ownership_controls "${own_ctl:-}" "$PIN_OWN_CTL"
pin_cmp controls    "${ctl_total:-}"   "$PIN_CONTROLS"
if [ "${green:-1}" -ne 0 ]; then
  echo "   !! PIN unusable_controls: observed ${green:-?}, expected 0"; pin_fail=1
fi
# ...and every profile must have actually produced a name comparison, so a silently skipped profile cannot pass.
n_cmp=$(find "$OUT" -maxdepth 1 -name 'got_*.txt' | wc -l)
pin_cmp name_comparisons "$n_cmp" "$PIN_PROFILES"
[ "$pin_fail" -eq 0 ] || rc=1
if [ "$rc" -eq 0 ]; then
  echo "PASS: probe + structural + controls all green"
else
  echo "FAILED — see above"
fi
exit $rc
