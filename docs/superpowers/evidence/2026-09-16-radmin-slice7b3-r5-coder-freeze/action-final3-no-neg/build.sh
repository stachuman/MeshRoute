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

# ================================================================================================================
# ★★★ §RADMIN SLICE 4 — TWO INDEPENDENTLY COMPILED PRODUCT ARMS, AND THE DRIVER IS A RE-EXEC RATHER THAN A LOOP.
#
#     R-RA-8 splits the product roles: a board is an ACCEPT target OR a CLIENT controller, never both, and
#     `src/firmware_commands.cpp` compiles a DIFFERENT set of bindings on each. One arm therefore cannot measure
#     the other: on a CLIENT build `acl`/`admin-id` are not router verbs at all, and on an ACCEPT build
#     `admin-key`/`admin-target` are not.
# ⛔ A `-D` OVERRIDE OF ONE TU AGAINST THE OTHER ARM'S SUPPORT OBJECTS WAS REFUSED: `mr_features.h` derives the
#    whole capability set from `MR_PROFILE_MOBILE`, so a half-flipped build would link two disagreeing worlds.
#    ⇒ EACH ARM COMPILES EVERY OBJECT — production TU, lib/core, lib/console, lib/hal, monocypher and the probe —
#    under ITS OWN defines, into ITS OWN `$OUT`, and the re-exec is what guarantees no state is shared.
#
# ★ THE ARM DEFINES ARE `platformio.ini`'s OWN DELTA, not a guessed list: `[env:heltec_mobile]` is literally
#   `extends = env:heltec_v3` plus `-DMR_PROFILE_MOBILE` (platformio.ini:524-528), and this probe's arm-1 define
#   set has been the heltec_v3 one since §CUSTODY-D. ⇒ arm 2 = arm 1 + that one flag, which is EXACTLY the
#   difference between the two real envs.
# ⚠⚠ ONE MEASURED LIMIT, STATED RATHER THAN GLOSSED: neither arm sets `-DMR_FEAT_OLED=1`, although both real envs
#    do. That is arm 1's pre-existing shape and it is NOT widened here, because it is not free: with OLED on,
#    `firmware_commands.cpp` references `mrfw::ui_emergency_active()`, which lives in `src/firmware_ui.cpp` — a TU
#    this probe does not compile and could not without the whole U8g2/board_ui canvas. MEASURED, not assumed:
#    the link fails with exactly that undefined symbol. The OLED axis of the console surface is covered instead by
#    `tools/probe_console_sink`, whose six-profile matrix includes `mobile_oled`.
# ================================================================================================================
if [ -z "${MR_PROBE_ARM:-}" ]; then
  arm_rc=0
  for _arm in accept client; do
    echo "================================================================================================"
    echo "== ARM: $_arm  (independently compiled; own defines, own objects, own outputs)"
    echo "================================================================================================"
    MR_PROBE_ARM="$_arm" bash "$HERE/run.sh" "$@" || arm_rc=1
  done
  echo
  if [ "$arm_rc" -eq 0 ]; then echo "BOTH ARMS PASS"; else echo "AN ARM FAILED — see above"; fi
  exit $arm_rc
fi
CXX=${CXX:-g++}
CC=${CC:-gcc}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

FW_CMDS="$ROOT/src/firmware_commands.cpp"   # THE ROUTER   — controls C1/C2 mutate this
FW_INBOX="$ROOT/src/firmware_inbox.cpp"     # THE HANDLER  — controls C3/C4 mutate this
FW_ACK="$ROOT/lib/console/console_json.h"   # the verdict -> lexeme mapping — control C5 mutates this
# §RADMIN-0b / [[B279]] — the files the `regen` supplied-sink half is asserted over.
FW_CMDS_H="$ROOT/src/firmware_commands.h"   # the ONE exported print_identity(..., Print&) declaration
LINE_SINK="$ROOT/src/dispatch_sink.h"       # the PRODUCTION LineSink the BLE arm ships through — control C11
FAKE_PREFS="$HERE/fakes/Preferences.h"      # the probe-local NV medium — controls C12/C13 make it LIE
FW_NVH="$ROOT/src/device_nv.h"              # §RADMIN slice 3: the typed wrappers — controls C27..C29 mutate this
FAKE_RNG="$HERE/fakes/esp_random.h"         # the probe-local deterministic entropy stream

DEFS=(-DARDUINO=100 -DMR_CONSOLE=1 -DBOARD_HELTEC_V3)
# ★ ARM 2 = ARM 1 + `platformio.ini`'s own `[env:heltec_v3]` -> `[env:heltec_mobile]` delta (see the header note).
[ "$MR_PROBE_ARM" = client ] && DEFS+=(-DMR_PROFILE_MOBILE)
# ★★ [[B321]]'s OBSERVATION SEAM, at LINK time: every `crypto_wipe` call is routed to the probe's `__wrap_` symbol,
#    which copies the bytes, performs the REAL wipe and re-reads the SAME LIVE storage. ⛔ Production is unmodified
#    and unaware; the wrapper is a straight forward unless a case arms it.
LDWRAP=(-Wl,--wrap=crypto_wipe -Wl,--wrap=_ZN9meshroute4Node10on_commandERKNS_7CommandE)
INCS=(-I"$HERE/fakes" -I"$HERE" -I"$ROOT/tools/probe_board_ui/fakes" -I"$ROOT/tools/probe_device_radio/fakes"
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
#
# ★★ §RADMIN-0b / [[B279]] RE-PIN: 39 -> 71 checks, 8 -> 14 controls. The R block adds THIRTY-TWO checks, counted
#    from the clean probe's own `  ok  ` lines and derived here so a deleted one is visible:
#      R1..R8   the /mrid SAVE FAILURE on the BLE-shaped sink   8  (owned · exact error bytes · no USB leak ·
#               no success prefix · identity unchanged · no crypto install · exactly one refused write ·
#               the record retained byte-for-byte)
#      R9..R10  the same failure on `mrcon`                     2  (byte-identical on USB · BLE stayed empty)
#      R11..R22 the SUCCESS on the BLE-shaped sink             12  (owned · exact success bytes · no USB leak ·
#               8 UPPERCASE hex · g_identity · node routing id · crypto_ready · one write · magic/version ·
#               fresh seed · name preserved · the medium retained exactly what production wrote)
#      R23..R24 the SUCCESS on `mrcon`                          2  (byte-identical to the BLE arm · BLE empty)
#      R25..R26 the optional-name boundary                      2  (no name · a MAXIMUM 32-B name)
#      R27..R30 the router boundary                             4  (`regen `, `regenerate`, `REGEN`, `rege`)
#      R31..R32 the BOOT formatter in its production shape      2  (exact banner bytes through `mrcon` · BLE empty)
#      8+2+12+2+2+4+2 = 32.  39 + 32 = 71. ✓
#    PIN_CONTROLS = 14 = the [[B237]] control-of-the-controls + C1..C7 (the §CUSTODY-D seven) + C8..C13 (the six
#    §0b ones: three production sink-restorations, one never-shipping LineSink, two DISHONEST storage fakes).
#
# ★★ §RADMIN-0c RE-PIN: 71 -> 90 checks, 14 -> 22 controls. The X block adds NINETEEN checks, counted from the clean
#    probe's own `  ok  ` lines and derived here so a deleted one is visible. They EXECUTE the one transport-neutral
#    seam `mrfw::exec_console_line()` through a REAL `GuardedConsole` and a REAL `LineSink`, in BOTH format arms —
#    which is the whole 0c wiring gate on the policy-bearing side (`src/fw_main.cpp` is host-uncompilable, so its two
#    one-call adapters are pinned STRUCTURALLY by tools/probe_console_sink/structural.py S22..S29 instead):
#      X1..X2   a router-owned command on each arm, 0 B cross-sink; the JSON arm streams through the REAL
#               LineSink and NEVER through the 256-B direct buffer                                        2
#      X3..X6   a parser-owned command: the TEXT envelope, the JSON envelope, no crossing, executed ONCE  4
#      X7..X10  `peerkey`/`peername` reach handle_peerkey/handle_peername (ack + exactly one NV write)    4
#      X11..X13 an accepted `reqpubkey` keeps its BLE-only event; the TEXT arm keeps the USB-only remedy
#               line and the JSON ack never carries prose                                                 3
#      X14..X16 empty / unknown / malformed ownership: the seam writes NOTHING and hands the caller a
#               typed `unmatched`/`empty` with the ParseErr                                               3
#      X17..X18 the supplied sink is the ONLY sink, on BOTH halves — the router arm (`dispatch` is HANDED the
#               sink) and the seam's own text rendering. ⛔ TWO rows because one was not enough: with only the
#               router row, the [[B279]]-shaped control C18 stayed GREEN, which is exactly the "a success that
#               isn't" shape this project keeps re-finding                                                 2
#      X19      the rendered reply is a COPY (the borrowed `Command::body` never outlives the call)        1
#      X20      `help` reaching the seam DOES stream the whole index — which is what makes the BLE
#               adapter's pre-seam `console_only` refusal load-bearing rather than decorative              1
#      2+4+4+3+3+2+1+1 = 20.  71 + 20 = 91. ✓
#    PIN_CONTROLS = 22 = 14 + C14..C21, the eight §0c mutations below.
#    ⛔ ONE CONTROL IS DELIBERATELY ABSENT AND SAYING SO IS THE POINT: "reverse the seam's router/parser order" has
#      NO behavioural control here, because with the measured-EMPTY intersection the reversal is INVISIBLE — that is
#      exactly why the unification was safe. Offering a green "order" control would be a vacuous check. The order is
#      pinned STRUCTURALLY (structural.py S24, with its own negctl control) and the emptiness it depends on is
#      re-derived every run by tools/probe_console_sink/ownership.py, which turns RED on a synthetic collision.
# ⚠⚠ RE-PINNED 2026-09-06 BY §RADMIN SLICE 3, 91 -> 130, AND THE +39 IS FULLY ATTRIBUTED — ⛔ not one prior row
#    was dropped or rewritten. The new rows are the target stores' REAL wiring, driven through `mrfw::dispatch()`:
#      R30/b/c/d   4  the two families are OWNED by the real router; `admin-key` (Slice 4's) and `aclx` are NOT
#      R31/b       2  an ABSENT store answers honestly, with 0 writes and 0 draws
#      R32..R32g   8  generate = EXACTLY ONE write, right namespace/key/size, no unrelated slot, valid stored
#                     record, a reload reproducing the same fp/pub, ⛔ no seed byte on the wire, 8 platform draws
#      R33         1  a second generate refuses `already_present` with 0 writes and 0 draws
#      R34 x5 + b  6  every confirm refusal costs 0 writes AND 0 entropy draws
#      R35..R35h   9  the first-owner ceremony, a second owner, set/unchanged/remove, the last-owner refusal, list
#      R36         1  a first owner cannot be granted without a valid administration root
#      R37/b       2  ★ an all-zero PLATFORM draw refuses `entropy_failed`, having ASKED, and mints nothing
#      R38         1  a refused medium answers `nv_save_failed` after exactly one attempt
#      R39/b/c     3  a corrupt record is `store_invalid`, ordinary writes cost 0, only `reset confirm` recovers
#      R40/b/c     3  the BOOT wrapper is read-only, reports both states, and prints no key or fingerprint
#      R41         1  the whole response lands on the SUPPLIED sink; `mrcon` and BLE get 0 B
#      R42..R42g   7  ★★ the `io_failed` arm, EXECUTED on the REAL ESP32 read sequence: a dead NVS (the namespace
#                     is not merely unwritten) makes BOTH records `io_failed` rather than the fresh-device
#                     `absent`, both consoles name it, ⛔ EVEN THE CONFIRM-GATED RECOVERIES refuse, and the boot
#                     report says so while writing nothing. This is what makes the typed wrappers' `&io` argument
#                     MEASURED rather than asserted — `--target=devicenv` cannot reach it (the host has no NV
#                     backend), so its cover moved here with controls C27..C29.
#    4+2+8+1+6+9+1+2+1+3+3+1+7 = 48 named rows; two of them (R34's five spellings) share one id, so the executed
#    count rises by 46: 91 + 46 = 137. ✓
# ★★ §RADMIN SLICE 4 — THE PINS ARE **PER ARM**, because the two arms measure two different products.
#   ★★ ACCEPT 146 -> 168 = +22 §RADMIN SLICE 5 rows, and the derivation is exact:
#        R32h/R32i     (2)  the generated pair is INSTALLED into the running `g_node`, and a root alone is not
#                           readiness — the two facts a callback count cannot distinguish;
#        S5-1a..S5-1f  (6)  the BOOT INSTALL: provisioning, activation, the one `admin-session boot` line, its
#                           zero writes, its zero key/fingerprint bytes and its idempotence;
#        S5-2a..S5-2f  (6)  SLOT-LOCAL vs ROOT-WIDE invalidation, measured on the REAL per-slot epochs;
#        S5-3a..S5-3c  (3)  PREPARE FAILS BEFORE THE WRITE — `runtime_unavailable`, zero writes, image intact;
#        S5-4a..S5-4b  (2)  NV FAILS AFTER A SUCCESSFUL PREPARE — the plan is discarded, the image is exact;
#        S5-5          (1)  ordinary `regen` preserves the administration root AND every session epoch;
#        S5-6a..S5-6b  (2)  a bad prerequisite installs the CLEARED image, never a stale live ACL;
#      ★★ +12 MORE at the [[B341]] fold-in (168 -> 180):
#        S5-7a..S5-7f  (6)  sequence B — generate, REBOOT with no ACL, `acl add owner` must ACTIVATE, and a
#                           fresh boot on the same medium must install exactly that readiness;
#        S5-8a..S5-8f  (6)  sequence D — a valid owner ACL with a CORRUPT root, REBOOT, `admin-id reset confirm`
#                           must ACTIVATE and the ACL must survive it.
#      ⓘ R32g was REPLACED IN PLACE (8 -> 28 platform draws), so it adds no row: the ten prepared epochs are
#        2 draws each and the count moving IS the evidence that the preparation really asks the platform.
#   ACCEPT 137 -> 146 = +8 [[B321]] rows (Z1..Z8: the EXECUTED `crypto_wipe` observation through the link-time
#   interposer, on success, on a post-allocation refusal, on the null input, plus the control that the interposer
#   is linked at all) +1 X8b (the `/mrpeers` wear guard, newly OBSERVABLE — see the X8 note in probe_main.cpp).
#   CLIENT 178 = 146 − 46 (the ACCEPT-only §RADMIN slice 3 rows, which are not a surface on a mobile product)
#   + 78 (the Q rows: the two families' router ownership and the TARGET half's absence, `show self` at zero reads,
#   the checked draw, the dead-draw and refused-medium arms, export, the ten counted ordinary refusals, the book
#   end to end over the ONE resident scratch, the read-only boot report, the io_failed arm including both
#   confirm-gated recoveries, the supplied-sink rule, and ★ the byte-identical preservation of BOTH controller
#   records across `regen` with its ruled warning).
# Slice 6: +182 on EACH arm, with all old rows retained: Y1..Y3=18, Y4..Y5=12,
# Y6=54, Y7=36 (18 refusals per format), Y8=54, Y9=6, Y10..Y11=2.
# Slice 7a/B360: eight shared A7-1..A7-8 typed config-store checks, no prior row removed.
# 7b-1: old ACCEPT 370 / CLIENT 368; +2 Y7 refusals per arm (factory_reset is
# disruptive even at owner), +8 shared R7-I rows. ACCEPT additionally +17 remote
# view rows and +374 real-radio checks (eight commands, actual jitter/CTS/DATA/ACK).
# 7b-2 ACCEPT: 771 + 592 = 1363. 7b-3's measured label census adds R7-A6 x6,
# R7-A29/A30 and R7-A13..A16 x1 each, and removes one refusal-only R7-A20: +12 -1 = +11.
# CLIENT stays 378 + local status dispatch and five absent target-field checks = 384.
PIN_CHECKS_ACCEPT=1374
PIN_CHECKS_CLIENT=384
PIN_CHECKS=$([ "$MR_PROBE_ARM" = client ] && echo "$PIN_CHECKS_CLIENT" || echo "$PIN_CHECKS_ACCEPT")
# ⚠ RE-PINNED 2026-09-06 BY §RADMIN SLICE 3, 22 -> 27: five controls on what the BINDINGS alone own — C22 the
#   dispatch arm deleted · C23 ★ the seed binding stops drawing from the platform · C24 the store binding stops
#   reading its record · C25 the Print adapter re-chooses `mrcon` ([[B279]]'s shape) · C26 the read-only boot
#   report starts writing. ⛔ Every prior control is preserved and still RED.
# ⚠ 27 -> 30: three more on the TYPED WRAPPERS, which `--target=devicenv` cannot reach at all — C27 `load_acl`
#   stops asking for SlotIo (a dead store reads as a fresh device) · C28 `save_acl` writes the WRONG slot (an ACL
#   update lands on the administration root) · C29 `load_admin_id` reads the wrong slot. The native suite is blind
#   to all three because the host arm has NO NV backend; here they run against the REAL ESP32 sequence.
# ★★ PER ARM as well: the slice-3 controls (C22..C29) mutate bindings a CLIENT build does not compile, and the
#   slice-4 ones (C30..C40) mutate bindings an ACCEPT build does not compile. A control that cannot bite on an arm
#   is `passes` — i.e. UNUSABLE — so each arm runs the 22 shared ones plus its own eight/eleven.
#   ACCEPT 30 = 22 shared + C22..C29 (8).   CLIENT 33 = 22 shared + C30..C40 (11).
PIN_CONTROLS_ACCEPT=60  # 7b-2: 50 + five service/open links and five exact status-value controls.
PIN_CONTROLS_CLIENT=45  # old 44 + B372; all previous controls retained.
PIN_CONTROLS=$([ "$MR_PROBE_ARM" = client ] && echo "$PIN_CONTROLS_CLIENT" || echo "$PIN_CONTROLS_ACCEPT")

# ---- the tree must not move -------------------------------------------------------------------------------------
# ⛔ SPELLED ONCE, IN A FUNCTION, AND THAT IS A FIX RATHER THAN TIDINESS: the sibling probe once had two `cat` lists
#    1470 lines apart, a slice added a file to one of them, and the tripwire reported "the probe MODIFIED a real
#    source" on a tree nothing had touched. A FALSE RED is as bad as a false green.
md5_sources() {
  cat "$FW_CMDS" "$FW_INBOX" "$FW_ACK" "$HERE/probe_main.cpp" \
      "$ROOT/tools/probe_board_ui/fakes/Arduino.h" "$ROOT/tools/probe_device_radio/fakes/RadioLib.h" \
      "$ROOT/src/firmware_config_parse.h" "$ROOT/lib/core/inbox.h" \
      "$FW_CMDS_H" "$LINE_SINK" "$FAKE_PREFS" "$FAKE_RNG" \
      "$HERE/remote_exec_rows.h" "$HERE/fakes/helpers/radiolib/CustomSX1262.h" \
      "$ROOT/src/firmware_remote_executor.h" "$ROOT/src/firmware_command_context.h" \
      "$ROOT/src/firmware_admin_verbs.h" "$ROOT/lib/core/remote_session.h" \
      "$ROOT/lib/core/remote_session.cpp" "$ROOT/lib/core/node.h" "$ROOT/lib/core/node.cpp" \
      "$ROOT/lib/core/node_mac_rx.cpp" "$ROOT/src/firmware_remote_actions.h" \
      "$ROOT/src/firmware_remote_actions.cpp" | md5sum | cut -d' ' -f1
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

# build_variant <router.cpp> <handler.cpp> <shadow-include-dir-or-empty> <out-binary>
# ⛔ The two production TUs are passed as PATHS so a control can hand in a MUTATED COPY of either without the probe
#   ever writing to the repository. A mutated HEADER is handed in as an include DIR that shadows the real one —
#   same principle, applied to a header, and §0b generalised the parameter from "the ack dir" to "the shadow dir"
#   because there are now THREE headers a control shadows: `console_json.h` (C5), `dispatch_sink.h` (C11) and the
#   probe's own `fakes/Preferences.h` (C12/C13). The dir is placed FIRST on the include path, ahead of both
#   `$HERE/fakes` and `$ROOT/src`, so the copy wins for every consumer in the build.
build_variant() {
  local router=$1 handler=$2 shadowdir=$3 bin=$4 probe=${5:-"$HERE/probe_main.cpp"}
  local actions=${6:-"$ROOT/src/firmware_remote_actions.cpp"}
  local pre=()
  [ -n "$shadowdir" ] && pre=(-I"$shadowdir")
  : > "$OUT/build.log"
  "$CXX" "${STD[@]}" -Wall -Wextra "${pre[@]}" "${DEFS[@]}" "${INCS[@]}" -c "$router" -o "$OUT/v_cmds.o" 2>>"$OUT/build.log" \
    && "$CXX" "${STD[@]}" -Wall -Wextra "${pre[@]}" "${DEFS[@]}" "${INCS[@]}" -c "$handler" -o "$OUT/v_inbox.o" 2>>"$OUT/build.log" \
    && "$CXX" "${STD[@]}" -Wall -Wextra "${pre[@]}" "${DEFS[@]}" "${INCS[@]}" -c "$actions" -o "$OUT/v_actions.o" 2>>"$OUT/build.log" \
    && "$CXX" "${STD[@]}" -Wall -Wextra "${pre[@]}" "${DEFS[@]}" "${INCS[@]}" -c "$probe" -o "$OUT/v_main.o" 2>>"$OUT/build.log" \
    && "$CXX" "$OUT/v_main.o" "$OUT/v_cmds.o" "$OUT/v_inbox.o" "$OUT/v_actions.o" "$OUT"/sup_*.o "${LDWRAP[@]}" -o "$bin" 2>>"$OUT/build.log"
}


LDWRAP+=(-Wl,--wrap=_ZN9meshroute4Node19clear_learned_stateEv)
build_support || exit 2
build_variant /tmp/mr-codex-s7b3-0gt630zl/gate/src/firmware_commands.cpp "$FW_INBOX" "" "$OUT/b0-p1" /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b0-p1.cpp || { cat "$OUT/build.log"; exit 2; }
cp "$OUT/build.log" /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b0-p1-compile.log
"$OUT/b0-p1" > /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b0-p1.log 2>&1 || { cat /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b0-p1.log; exit 1; }
cat /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b0-p1.log
build_variant /tmp/mr-codex-s7b3-0gt630zl/gate/src/firmware_commands.cpp "$FW_INBOX" "" "$OUT/b1-p1" /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b1-p1.cpp || { cat "$OUT/build.log"; exit 2; }
cp "$OUT/build.log" /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b1-p1-compile.log
"$OUT/b1-p1" > /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b1-p1.log 2>&1 || { cat /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b1-p1.log; exit 1; }
cat /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b1-p1.log
build_variant /tmp/mr-codex-s7b3-0gt630zl/gate/src/firmware_commands.cpp "$FW_INBOX" "" "$OUT/b2-p1" /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b2-p1.cpp || { cat "$OUT/build.log"; exit 2; }
cp "$OUT/build.log" /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b2-p1-compile.log
"$OUT/b2-p1" > /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b2-p1.log 2>&1 || { cat /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b2-p1.log; exit 1; }
cat /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b2-p1.log
build_variant /tmp/mr-codex-s7b3-0gt630zl/gate/src/firmware_commands.cpp "$FW_INBOX" "" "$OUT/b2-p0" /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b2-p0.cpp || { cat "$OUT/build.log"; exit 2; }
cp "$OUT/build.log" /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b2-p0-compile.log
"$OUT/b2-p0" > /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b2-p0.log 2>&1 || { cat /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b2-p0.log; exit 1; }
cat /tmp/mr-codex-s7b3-0gt630zl/action-final3-no-neg/b2-p0.log
