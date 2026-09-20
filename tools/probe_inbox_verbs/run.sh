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
# Slice 10: +20 shared migration/golden rows (three four-slot loops + eight scalar checks).
PIN_CHECKS_ACCEPT=1394
# 8ac: 47 executed real-router/local-delivery and selected-key wipe checks.
# 8b adds ten observer/transport checks and eight real-Node binding-veto checks.
PIN_CHECKS_CLIENT=477
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
# 7b-2: 50 + five service/open links and five exact status-value controls.
# Slice 10: +A10-C1 config wrong-slot read, on both arms; every prior control retained.
PIN_CONTROLS_ACCEPT=61
# 8ac adds eleven real firmware decision controls to the previous 45.
# 8b adds debt status, carrier write binding, disconnect-drop and real Node lookup controls.
PIN_CONTROLS_CLIENT=69
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
      "$ROOT/src/firmware_remote_actions.cpp" "$ROOT/src/firmware_remote_client.cpp" \
      "$ROOT/src/firmware_remote_client.h" "$HERE/remote_client_rows.h" | md5sum | cut -d' ' -f1
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
  local client=${7:-"$ROOT/src/firmware_remote_client.cpp"}
  local pre=()
  [ -n "$shadowdir" ] && pre=(-I"$shadowdir")
  : > "$OUT/build.log"
  "$CXX" "${STD[@]}" -Wall -Wextra "${pre[@]}" "${DEFS[@]}" "${INCS[@]}" -c "$router" -o "$OUT/v_cmds.o" 2>>"$OUT/build.log" \
    && "$CXX" "${STD[@]}" -Wall -Wextra "${pre[@]}" "${DEFS[@]}" "${INCS[@]}" -c "$handler" -o "$OUT/v_inbox.o" 2>>"$OUT/build.log" \
    && "$CXX" "${STD[@]}" -Wall -Wextra "${pre[@]}" "${DEFS[@]}" "${INCS[@]}" -c "$actions" -o "$OUT/v_actions.o" 2>>"$OUT/build.log" \
    && "$CXX" "${STD[@]}" -Wall -Wextra "${pre[@]}" "${DEFS[@]}" "${INCS[@]}" -c "$client" -o "$OUT/v_client.o" 2>>"$OUT/build.log" \
    && "$CXX" "${STD[@]}" -Wall -Wextra "${pre[@]}" "${DEFS[@]}" "${INCS[@]}" -c "$probe" -o "$OUT/v_main.o" 2>>"$OUT/build.log" \
    && "$CXX" "$OUT/v_main.o" "$OUT/v_cmds.o" "$OUT/v_inbox.o" "$OUT/v_actions.o" "$OUT/v_client.o" "$OUT"/sup_*.o "${LDWRAP[@]}" -o "$bin" 2>>"$OUT/build.log"
}

rc=0
if ! build_support; then echo "FAIL — the support half did not build"; exit 1; fi
if ! build_variant "$FW_CMDS" "$FW_INBOX" "" "$OUT/probe"; then
  echo "PROBE BUILD FAILED — the real src/ TUs did not host-compile:"
  sed 's/^/    /' "$OUT/build.log" | head -25
  exit 1
fi
# Link-level absence, not a synthetically disabled runtime role. Both arms link the
# same source list under their own real profile; the executor must exist only on ACCEPT.
nm -C "$OUT/probe" > "$OUT/symbols.txt"
if [ "$MR_PROBE_ARM" = client ]; then
  if grep -Eq 'mrfw::(remote_executor_service_once|radmin_service_once|\(anonymous namespace\)::Remote(Target|Exec))' "$OUT/symbols.txt"; then
    echo 'FAIL — CLIENT contains executor symbols'; exit 1
  fi
else
  if ! grep -q 'mrfw::remote_executor_service_once()' "$OUT/symbols.txt"; then
    echo 'FAIL — ACCEPT executor binding missing'; exit 1
  fi
fi
echo "executor symbol ownership: $MR_PROBE_ARM PASS"
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

# ctl <label> <which:router|handler|ack|sink|prefs> <sed-script>
# ⓘ §0b added three SHADOW-HEADER kinds beside `ack`. Each writes ONE mutated copy under $OUT/shadow (wiped first,
#   so a stale header from a previous control can never join a later build) and hands that dir to build_variant.
ctl() {
  local label=$1 which=$2 script=$3
  local router="$FW_CMDS" handler="$FW_INBOX" shadowdir="" probe="$HERE/probe_main.cpp"
  local client="$ROOT/src/firmware_remote_client.cpp"
  shadow_hdr() {   # shadow_hdr <real-header> <basename> -> writes $OUT/shadow/<basename>, sets shadowdir
    rm -rf "$OUT/shadow"; mkdir -p "$OUT/shadow"
    sed "$script" "$1" > "$OUT/shadow/$2"; shadowdir="$OUT/shadow"
    cmp -s "$1" "$OUT/shadow/$2"
  }
  case "$which" in
    client) sed "$script" "$client" > "$OUT/mutant_client.cpp"
            cmp -s "$client" "$OUT/mutant_client.cpp" && { n_bad=$((n_bad+1)); echo "  FAIL $label VACUOUS"; return; }
            client="$OUT/mutant_client.cpp" ;;
    router)  sed "$script" "$FW_CMDS"  > "$OUT/mutant_cmds.cpp";  router="$OUT/mutant_cmds.cpp"
             cmp -s "$FW_CMDS" "$router"  && { n_bad=$((n_bad+1)); printf '  FAIL %s — the mutation changed NOTHING (VACUOUS)\n' "$label"; return; } ;;
    handler) sed "$script" "$FW_INBOX" > "$OUT/mutant_inbox.cpp"; handler="$OUT/mutant_inbox.cpp"
             cmp -s "$FW_INBOX" "$handler" && { n_bad=$((n_bad+1)); printf '  FAIL %s — the mutation changed NOTHING (VACUOUS)\n' "$label"; return; } ;;
    probe)   sed "$script" "$HERE/probe_main.cpp" > "$OUT/mutant_probe.cpp"; probe="$OUT/mutant_probe.cpp"
             cmp -s "$HERE/probe_main.cpp" "$probe" && { n_bad=$((n_bad+1)); printf '  FAIL %s — the mutation changed NOTHING (VACUOUS)\n' "$label"; return; } ;;
    ack)     shadow_hdr "$FW_ACK"    console_json.h  && { n_bad=$((n_bad+1)); printf '  FAIL %s — the mutation changed NOTHING (VACUOUS)\n' "$label"; return; } ;;
    sink)    shadow_hdr "$LINE_SINK" dispatch_sink.h && { n_bad=$((n_bad+1)); printf '  FAIL %s — the mutation changed NOTHING (VACUOUS)\n' "$label"; return; } ;;
    prefs)   shadow_hdr "$FAKE_PREFS" Preferences.h  && { n_bad=$((n_bad+1)); printf '  FAIL %s — the mutation changed NOTHING (VACUOUS)\n' "$label"; return; } ;;
    # ★★ §RADMIN slice 3: `src/device_nv.h`'s typed wrappers — and this kind needs a WHOLE-`src/` shadow rather
    #    than the one-header kind above, for a C++ reason worth recording: a QUOTED include resolves against the
    #    INCLUDING FILE'S OWN DIRECTORY FIRST, so a lone `$OUT/shadow/device_nv.h` is picked up by `probe_main.cpp`
    #    (which is not in `src/`) while `src/firmware_commands.cpp` keeps resolving to the REAL one. The result is
    #    TWO files defining `mrnv::Blob` and a hundred redefinition errors — i.e. a control that reports "does not
    #    compile" and measures nothing. ⇒ the copy carries the whole directory and the router/handler are compiled
    #    FROM it, so exactly one `device_nv.h` exists in the translation unit.
    nvh)     rm -rf "$OUT/srcshadow"; cp -r "$ROOT/src" "$OUT/srcshadow"
             sed "$script" "$FW_NVH" > "$OUT/srcshadow/device_nv.h"
             cmp -s "$FW_NVH" "$OUT/srcshadow/device_nv.h" && { n_bad=$((n_bad+1)); printf '  FAIL %s — the mutation changed NOTHING (VACUOUS)\n' "$label"; return; }
             shadowdir="$OUT/srcshadow"
             router="$OUT/srcshadow/$(basename "$FW_CMDS")"
             handler="$OUT/srcshadow/$(basename "$FW_INBOX")" ;;
    # ★★ §RADMIN slice 4 / [[B321]] — `src/firmware_config_parse.h`, and it needs the SAME whole-`src/` shadow the
    #    `nvh` kind needs, for the identical C++ reason: a quoted include resolves against the INCLUDING file's own
    #    directory first, so a lone shadow header would be picked up by `probe_main.cpp` while the production TUs
    #    kept resolving to the real one — two definitions of `mrfw::parse_hex32` in one link.
    cfgp)    rm -rf "$OUT/srcshadow"; cp -r "$ROOT/src" "$OUT/srcshadow"
             sed "$script" "$ROOT/src/firmware_config_parse.h" > "$OUT/srcshadow/firmware_config_parse.h"
             cmp -s "$ROOT/src/firmware_config_parse.h" "$OUT/srcshadow/firmware_config_parse.h" && { n_bad=$((n_bad+1)); printf '  FAIL %s — the mutation changed NOTHING (VACUOUS)\n' "$label"; return; }
             shadowdir="$OUT/srcshadow"
             router="$OUT/srcshadow/$(basename "$FW_CMDS")"
             handler="$OUT/srcshadow/$(basename "$FW_INBOX")" ;;
  esac
  if ! build_variant "$router" "$handler" "$shadowdir" "$OUT/mutant.bin" "$probe" "" "$client"; then
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
    # ⓘ §0b: the failing rows are NAMED, the `tools/probe_ble_line/run.sh` idiom (U3) — "RED (5 failed)" cannot be
    #   audited against the claim a control makes, and a control that reddens the WRONG rows is a control that
    #   measures something other than what its label says.
    red)      n_ctl=$((n_ctl+1))
              local red_rows; red_rows=$(grep '^  FAIL ' "$OUT/mutant.out" | awk '{print $2}' | tr '\n' ' ')
              printf '  ok   %s -> RED (%s check(s) failed: %s)\n' "$label" "$fails" "$red_rows" ;;
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

  # ================================ §RADMIN slice 3 — the target stores' wiring controls ========================
  # ⛔ ACCEPT ARM ONLY: on a CLIENT build the target-store bindings are not compiled at all, so each mutation below
  #    would change a block the product does not have — the probe would stay GREEN and the control would score as
  #    `passes`, i.e. UNUSABLE. A control that cannot bite on an arm does not belong to that arm.
  if [ "$MR_PROBE_ARM" = accept ]; then
  # ★ EACH IS THE TEMPTING WRONG EDIT, applied to a COPY of the REAL source, and each must turn the probe RED on
  #   the rows it is aimed at. A mutant that fails to build, dies, or passes is UNUSABLE — never a scored control.

  # ---- C14: THE ROUTER ARM REMOVED. The whole family becomes unreachable and every one of the 38 new rows loses
  #           its subject — the shape C1 closes for `clear_inbox`, one family over.
  ctl 'C22 the §RADMIN target-store DISPATCH ARM is deleted (both families unreachable)' router \
      '/if (admin_router_arm(line, len, out)) return true;/d'

  # ---- C23: ★ THE ENTROPY BINDING STOPS ASKING THE PLATFORM. It answers `true` and leaves the caller's buffer
  #           as the service initialised it — the shape a "cache the seed" or "derive it from the id" refactor
  #           would take. ⛔ INVISIBLE to every native test: the binding lives in this TU and nothing else compiles
  #           it. It reddens the DRAW COUNT (R32g) and the dead-source refusal's "it ASKED" half (R37).
  # ⓘ WHY NOT `return true;` ALONE — measured, and worth recording: the all-zero refusal is guarded TWICE, once in
  #   this binding (`!admin_buf_all_zero`) and once inside `AdminIdService::mint_` (`admin_id_content_valid`), so
  #   deleting either belt alone leaves the console answer IDENTICAL and the control would prove nothing. That
  #   redundancy is deliberate defence in depth; the SERVICE half is attacked individually by
  #   `probe_ui_model_mutations.py --target=radmin3id`, and this control attacks what the binding alone owns —
  #   actually drawing from the platform.
  ctl 'C23 ★ the seed binding STOPS DRAWING from the platform (answers true, fills nothing)' router \
      's|        mrrng::fill(out, 32);|        (void)out;|'

  # ---- C16: THE STORE BINDING IS POINTED AT THE WRONG RECORD. The service is perfect; the adapter reads and
  #           writes `/mrid` instead. No pure test can see this — the binding is glue.
  ctl 'C24 the admin-identity store binding stops reading the record it was bound to' router \
      's|    mrnv::AdminIdRead load(mrnv::AdminIdBlob\& out) override { return mrnv::load_admin_id(out); }|    mrnv::AdminIdRead load(mrnv::AdminIdBlob\& out) override { (void)out; return mrnv::AdminIdRead::absent; }|'

  # ---- C17: THE PRINT ADAPTER RE-CHOOSES THE GLOBAL CONSOLE. The [[B279]] shape, one family over: the response
  #           is correct and complete, and it goes to the WRONG sink — so a BLE caller would receive nothing.
  ctl 'C25 the target-store Print adapter writes to `mrcon` instead of the SUPPLIED sink ([[B279]] shape)' router \
      's|_o.write(reinterpret_cast<const uint8_t\*>(s), n); }   // §RADMIN-3 sink|mrcon.write(reinterpret_cast<const uint8_t*>(s), n); }   // §RADMIN-3 sink|'

  # ---- C18: THE BOOT REPORT STARTS WRITING. Design §6.4 forbids inventing an active owner; a boot that seeded
  #           an empty ACL would do exactly that on the first transient read failure.
  # ---- C27..C29: the TYPED WRAPPERS. ⛔ None of these three is reachable from `--target=devicenv`: that battery
  #      runs the NATIVE suite and the host has no NV backend, so a re-pointed slot and a dropped `SlotIo` are both
  #      invisible there. They are controlled HERE, against the REAL ESP32 read/write sequence.
  ctl 'C27 ★ `load_acl` stops asking the primitive for SlotIo — a DEAD store reads as a fresh device' nvh \
      's|    const int n = read_slot(kSlotAcl, \&out, sizeof out, \&io);|    const int n = read_slot(kSlotAcl, \&out, sizeof out);|'

  ctl 'C28 ★★ `save_acl` WRITES THE WRONG SLOT — an ACL update lands on the administration root' nvh \
      's|inline bool save_acl(const AclBlob\& b) { return write_slot(kSlotAcl, \&b, sizeof b); }|inline bool save_acl(const AclBlob\& b) { return write_slot(kSlotAdmid, \&b, sizeof b); }|'

  ctl 'C29 ★★ `load_admin_id` READS THE WRONG SLOT — the ACL bytes are classified as an administration root' nvh \
      's|    const int n = read_slot(kSlotAdmid, \&out, sizeof out, \&io);|    const int n = read_slot(kSlotAcl, \&out, sizeof out, \&io);|'

  ctl 'C26 the READ-ONLY boot report starts WRITING (an auto-seed on a fresh device)' router \
      's|    mrfw::admin_boot_report(id, acl, lines);|    mrfw::admin_boot_report(id, acl, lines);\n    (void)mrnv::save_acl(mrnv::AclBlob{});|'
  fi

  # ================================ §RADMIN slice 4 — the CONTROLLER stores' wiring controls ====================
  # ⛔ CLIENT ARM ONLY, for the mirror reason. ★ Each is the tempting WRONG EDIT on the controller half, and the
  #   two starred ones are the shapes design §6.4 and R-RA-30 exist to forbid.
  if [ "$MR_PROBE_ARM" = client ]; then
  ctl 'C30 the §RADMIN controller DISPATCH ARM is deleted (both families unreachable)' router \
      '/if (admin_client_router_arm(line, len, out)) return true;/d'

  # ⓘ RANGE-ADDRESSED to the CONTROLLER struct: `sed` is line-oriented, so a `\n` in the FIND text matches nothing
  #   (a vacuous control, which is an instrument failure, not a result). The address also keeps the mutation off
  #   the byte-identical ACCEPT binding twenty lines up, so the reddening is attributable to ONE product role.
  ctl 'C31 ★ the controller seed binding STOPS DRAWING from the platform (answers true, fills nothing)' router \
      '/struct DeviceMgmtKeySeed/,/^};/ s|        mrrng::fill(out, 32);|        (void)out;|'

  ctl 'C32 the keyring store binding stops reading the record it was bound to (every list reads a fresh device)' router \
      's|    mrnv::MgmtKeyRead load(mrnv::MgmtKeyBlob\& out) override { return mrnv::load_mgmt_keys(out); }|    mrnv::MgmtKeyRead load(mrnv::MgmtKeyBlob\& out) override { (void)out; return mrnv::MgmtKeyRead::absent; }|'

  ctl 'C33 the controller Print adapter writes to `mrcon` instead of the SUPPLIED sink ([[B279]] shape)' router \
      's|    void line(const char\* s, size_t n) override { _o.write(reinterpret_cast<const uint8_t\*>(s), n); }   // §RADMIN-4 sink|    void line(const char* s, size_t n) override { mrcon.write(reinterpret_cast<const uint8_t*>(s), n); }|'

  ctl 'C34 the READ-ONLY controller boot report starts WRITING (an auto-generate on a fresh controller)' router \
      's|    mrfw::admin_client_boot_report(keys, targets, s_targets, lines);|    mrfw::admin_client_boot_report(keys, targets, s_targets, lines);\n    (void)mrnv::save_targets(mrnv::TargetBlob{});|'

  ctl 'C35 ★★ do_regen REWRITES the keyring — the ruled `keys and targets preserved` warning becomes a LIE' router \
      's|    out.print(F("> regen ok"));|    { mrnv::MgmtKeyBlob mk{}; mrnv::mgmt_key_blob_init(mk); (void)mrnv::save_mgmt_keys(mk); }\n    out.print(F("> regen ok"));|'

  ctl 'C36 the CLIENT regen WARNING is emitted on the FAILURE path too (a rotation that did not happen is warned about)' router \
      's|    if (!mrnv::save_id(idb)) { out.println(F("> regen err nv_save_failed")); return; }|    if (!mrnv::save_id(idb)) { out.println(F("> regen err nv_save_failed")); { AdminClientPrintLines l2(out); mrfw::client_regen_emit_note(l2); } return; }|'

  # ⓘ ONE LINE, for C31's reason. Dropping the `&io` argument alone is the whole defect: `io` then stays
  #   default-constructed, `backend_failed` is never set, and a store that would not answer reads as ABSENT.
  ctl 'C37 ★ `load_mgmt_keys` stops asking the primitive for SlotIo — a DEAD store reads as a fresh device' nvh \
      's|    const int n = read_slot(kSlotMgmtKeys, \&out, sizeof out, \&io);|    const int n = read_slot(kSlotMgmtKeys, \&out, sizeof out, nullptr);|'

  ctl 'C38 ★★ `save_targets` WRITES THE WRONG SLOT — a book update lands on the management keyring' nvh \
      's|inline bool save_targets(const TargetBlob\& b) { return write_slot(kSlotTargets, \&b, sizeof b); }|inline bool save_targets(const TargetBlob\& b) { return write_slot(kSlotMgmtKeys, \&b, sizeof b); }|'

  ctl 'C39 ★★★ `load_mgmt_keys` READS THE TARGET-SIDE `/mradmid` SLOT — design §6.4'"'"'s two seeds CROSS' nvh \
      's|    const int n = read_slot(kSlotMgmtKeys, \&out, sizeof out, \&io);|    const int n = read_slot(kSlotAdmid, \&out, sizeof out, \&io);|'

  ctl 'C40 ★★ [[B321]]: the shared decoder'"'"'s SECRET-SCRATCH guard is DELETED — the key material is left on the stack' cfgp \
      's|    } hex_scratch_wipe{buf};|    };|'
  fi


  # ---- C6: THE CLEAR IS NEVER PERFORMED but the ack still claims it. The mirror image of C5: an ack that reports
  #          a destruction the handler declined to do. Neither pure units nor a structural grep can see this.
  ctl 'C6  `ib.clear()` is replaced by an unconditional success (the ack claims a clear that never ran)' handler \
      's/const bool cleared = ib.clear();/const bool cleared = true;/'

  # ---- C7: THE ARM ROUTES TO THE WRONG HANDLER — the "insert beside its neighbours" property, attacked directly.
  ctl 'C7  the dispatch arm routes `clear_inbox` to `handle_del_msg` instead' router \
      's/{ handle_clear_inbox(line + 11, len - 11, out); return true; }/{ handle_del_msg(line + 11, out); return true; }/'

  # ============================================================ §RADMIN-0b / [[B279]] — THE SUPPLIED-SINK CONTROLS
  # ⛔ C8/C9/C10 are the THREE HALVES of the defect, each restored on its own. They are deliberately separate: the
  #    defect had three independent places where the global console could come back (the dispatch arm, do_regen's
  #    own two writes, and the shared formatter), and a single combined control would let two of them regress
  #    unnoticed behind the third.

  # ---- C8: THE ARM DISCARDS ITS SINK AGAIN — [[B279]] exactly as it was found. The USB drive stays green and the
  #          BLE drive goes EMPTY while `Serial` receives the leak, which is why a USB-only gate never saw it.
  ctl 'C8  the dispatch arm hands `do_regen` the GLOBAL console instead of its supplied `out` ([[B279]] restored)' router \
      's|{ do_regen(out); return true; }|{ do_regen(mrcon); return true; }|'

  # ---- C9: do_regen's OWN writes (the success prefix + the nv_save_failed line) go back to the global console,
  #          while the delegated formatter still honours the sink -> a SPLIT response, the shape a partial fix takes.
  ctl 'C9  `do_regen`'"'"'s own success/error writes are routed back to `mrcon`' router \
      '/^static void do_regen(Print& out) {$/,/^}$/ s/\bout\./mrcon./g'

  # ---- C10: the shared FORMATTER goes back to the global console. ★ The boot banner (R31) stays GREEN under this
  #           mutation — which is the entire reason the BLE-shaped rows had to exist: only they catch it.
  ctl 'C10 `print_identity`'"'"'s body is routed back to `mrcon` (the identity tail leaks, the prefix does not)' router \
      '/^void print_identity(const mrnv::IdBlob& idb, Print& out) {$/,/^}$/ s/\bout\./mrcon./g'

  # ---- C11: THE SINK NEVER SHIPS. Proves the BLE rows observe bytes that LEFT the sink through its flush callback,
  #           not bytes staged inside it. ⓘ BOTH ship paths are neutered on purpose, and that is a MEASURED
  #           correction to the brief's literal "suppress LineSink::flush()": every `regen` line ends `\r\n` and
  #           `LineSink::write` ships on '\n' (src/dispatch_sink.h), so suppressing flush() ALONE is provably
  #           vacuous here — it would be a control that proves nothing, which is the one thing a control may not be.
  ctl 'C11 the production `LineSink` never ships (neither on newline nor on flush)' sink \
      's|_flush(_buf, _len); _len = 0; }   // ship on newline|_len = 0; }   // ship on newline|;
       s|void flush() { if (_len) { _flush(_buf, _len); _len = 0; } }|void flush() { if (_len) { _len = 0; } }|'

  # ---- C12/C13: THE PROBE REFUSES ITS OWN DISHONEST STORAGE INSTRUMENT. A fake that keeps bytes it reported as
  #               unwritten, or reports a write it never made, would let a broken `regen` read as correct. Each
  #               switch is flipped in a COPY of the probe's own Preferences fake and must turn the probe RED.
  ctl 'C12 the NV fake RETAINS the record while reporting the write FAILED (dishonest storage)' prefs \
      's|bool retain_on_fail = false;|bool retain_on_fail = true;|'

  ctl 'C13 the NV fake reports a SUCCESSFUL write while retaining nothing (dishonest storage)' prefs \
      's|bool drop_on_ok     = false;|bool drop_on_ok     = true;|'

  # =========================================================== §RADMIN-0c — THE EXECUTION SEAM'S OWN CONTROLS
  # ⛔ Each is a TEMPTING WRONG SHAPE of `mrfw::exec_console_line()`, not a deletion: an envelope swapped, a handler
  #    bypassed, a transport-specific field dropped, a sink re-chosen, an ownership state collapsed, a command run
  #    twice. Each must turn the X rows RED, and the failing rows are NAMED so the claim can be audited.

  # ---- C14: THE TWO FORMAT ARMS SWAPPED. USB gets the companion's NDJSON and the companion gets human text — the
  #          single most likely way a transport-neutral seam stops being transport-correct.
  ctl 'C14 the seam renders each transport in the OTHER format (`fmt == json` -> `fmt == text`)' router \
      's|if (fmt == LineFormat::json) {|if (fmt == LineFormat::text) {|'

  # ---- C15: `peerkey` BYPASSES ITS ESTABLISHED HANDLER and is executed as a bare Node command. The RAM install
  #          still happens, but /mrpeers is never mirrored and the contract ack becomes a generic {"ack":…}.
  ctl 'C15 `peerkey` is executed as a bare Node command instead of through handle_peerkey' router \
      's|{ r.n = handle_peerkey(reply, reply_cap, cmd);  return r; }|{ r.n = meshroute::console::write_ack(reply, reply_cap, g_node.on_command(cmd)); return r; }|'

  # ---- C16: THE BLE-SPECIFIC EVENT IS LOST. An accepted `reqpubkey` answers the generic ack, so the companion can
  #          no longer tell "the query flew" from "the command was accepted and did nothing".
  ctl 'C16 an accepted `reqpubkey` loses its reqpubkey_sent event (falls back to write_ack)' router \
      's|r.n = meshroute::console::write_reqpubkey_sent(reply, reply_cap, cr.dst_hash, cr.plane);|r.n = meshroute::console::write_ack(reply, reply_cap, cr);|'

  # ---- C17: THE USB-ONLY REMEDY LINE IS DROPPED. The refusal still names its CmdCode, but the operator loses the
  #          way round it — the §id-hash S1 defect, restored.
  ctl 'C17 the TEXT arm drops print_reqpubkey_hint (a refusal names the wall but not the way round it)' router \
      's|    print_reqpubkey_hint(stream, cmd, cr);||'

  # ---- C18: THE SEAM RE-CHOOSES THE SINK. Every text-arm write goes to the global console instead of the `Print&`
  #          it was handed — [[B279]] exactly as it was found, one layer up.
  ctl 'C18 the TEXT arm writes to the global `mrcon` instead of the sink it was handed ([[B279]] shape)' router \
      '/^LineExec exec_console_line/,/^}$/ s/\bstream\./mrcon./g'

  # ---- C19: AN OWNERSHIP STATE IS COLLAPSED. An empty line comes back `unmatched`, so both transports would answer
  #          a bare Enter with a parse error instead of silence.
  ctl 'C19 the `empty` completion is collapsed into `unmatched` (a bare Enter would answer an error)' router \
      's|{ r.state = LineExec::State::empty;     return r; }|{ r.state = LineExec::State::unmatched; return r; }|'

  # ---- C20: A STREAMED ROUTER RESPONSE IS REPORTED AS BUFFERED. The BLE adapter would then return `ex.n` (0) and
  #          never flush — the bytes are in the sink, the transport is told nothing happened.
  ctl 'C20 a router response is reported as `buffered` instead of `streamed` (the caller would not flush)' router \
      's|if (dispatch(line, len, stream, ctx.transport)) { r.state = LineExec::State::streamed; r.outcome = DispatchOutcome::completed; return r; }|if (dispatch(line, len, stream, ctx.transport)) { r.state = LineExec::State::buffered; r.outcome = DispatchOutcome::completed; return r; }|'

  # ---- C21: THE COMMAND IS EXECUTED TWICE on the text arm — two counters burned, two frames queued, one answer
  #          printed. The failure a byte-comparison alone cannot see, which is why the X rows measure the counter.
  ctl 'C21 the TEXT arm executes the Node command TWICE (two ctrs burned, one answer printed)' router \
      '/^    r.state = LineExec::State::streamed;$/,$ s|const meshroute::CmdResult cr = g_node.on_command(cmd);|g_node.on_command(cmd); const meshroute::CmdResult cr = g_node.on_command(cmd);|'

  # New controls must have exactly one literal source anchor before sed applies their edit.
  s6_ctl() {
    local label=$1 needle=$2 script=$3
    if ! python3 -c 'import pathlib,sys; n=pathlib.Path(sys.argv[1]).read_text().count(sys.argv[2]); print("    source match count",n); sys.exit(0 if n==1 else 1)' "$FW_CMDS" "$needle"; then
      n_bad=$((n_bad+1)); echo "  FAIL $label — not exactly one source match"; return
    fi
    ctl "$label" router "$script"
  }
  s6_ctl 'S6-C1 seam validator bypassed' \
    'r.line_err = meshroute::console::validate_command_line(line, len, ctx.line_max_bytes);' \
    's|r.line_err = meshroute::console::validate_command_line(line, len, ctx.line_max_bytes);|r.line_err = meshroute::console::LineErr::ok;|'
  s6_ctl 'S6-C2 remote policy bypassed' \
    'if (ctx.authority != CommandAuthority::local) {' \
    's|if (ctx.authority != CommandAuthority::local) {|if (false) {|'
  s6_ctl 'S6-C3 local contexts consult the remote table' \
    'if (ctx.authority != CommandAuthority::local) {' \
    's|if (ctx.authority != CommandAuthority::local) {|if (ctx.authority == CommandAuthority::local) {|'
  s6_ctl 'S6-C4 remote admission inverted' \
    '!command_authority_admits(*policy, ctx, line, len)' \
    's|!command_authority_admits(\*policy, ctx, line, len)|command_authority_admits(*policy, ctx, line, len)|'
  s6_ctl 'S6-C5 remote refusal emits local bytes' \
    'r.refuse = policy ? RefuseReason::authority : RefuseReason::unclassified;' \
    's|r.refuse = policy ? RefuseReason::authority : RefuseReason::unclassified;|r.refuse = policy ? RefuseReason::authority : RefuseReason::unclassified; stream.print("refused");|'
  s6_ctl 'S6-C6 real panel validator bypassed' \
    'r.line_err = meshroute::console::validate_command_line(line, len, meshroute::console::local_command_max_bytes);' \
    's|r.line_err = meshroute::console::validate_command_line(line, len, meshroute::console::local_command_max_bytes);|r.line_err = meshroute::console::LineErr::ok;|'
  s6_ctl 'S6-C7 parser success loses its typed completed outcome' \
    $'\n    r.outcome = DispatchOutcome::completed;\n' \
    's|^    r.outcome = DispatchOutcome::completed;$|    r.outcome = DispatchOutcome::unmatched;|'
  s6_ctl 'S6-C8 JSON bad_line envelope changes its reason domain' \
    'write_err(reply, reply_cap, "bad_line", meshroute::console::line_err_name(r.line_err))' \
    's|write_err(reply, reply_cap, "bad_line",|write_err(reply, reply_cap, "parse",|'
  s6_ctl 'S6-C9 remote byte-validation refusal emits a local envelope' \
    'if (ctx.authority != CommandAuthority::local) return r;' \
    's|if (ctx.authority != CommandAuthority::local) return r;|(void)ctx;|'
fi

if [ "${1:-}" != "--no-neg" ]; then
  ctl 'R7-C1 B372 eight-byte truncation restored in medium (positive contents must fail)' probe \
    's|rec\[n_rec\].len = len;|rec[n_rec].len = len < 8 ? len : 8;|'
  if [ "$MR_PROBE_ARM" = accept ]; then
    ctl 'R7-C2 private DM filter bypassed' handler \
      's|&& !meshroute::inbox_record_is_internal(e.type))|\&\& false)|'
    ctl 'R7-C3 blanket DM-kind filter wrongly hides both diagnostics' handler \
      's|&& !meshroute::inbox_record_is_internal(e.type))|\&\& true)|'
    ctl 'R7-C4 remote mark_read changes shared DM cursor' handler \
      '/if (remote_inbox_refuses(kind, "mark_read", out)) return;/d'
    ctl 'R7-C5 remote del_msg deletes private DM' handler \
      '/if (remote_inbox_refuses(kind, "del_msg", out)) return;/d'
    ctl 'R7-C6 seam stops publishing authenticated context' router \
      '/CommandContextScope scope(ctx);/d'
    ctl 'R7-C7 transcript Print adapter leaks to local console' router \
      's|size_t write(const uint8_t\* p, size_t n) override { sink.append(p, n); return n; }|size_t write(const uint8_t* p, size_t n) override { mrcon.write(p, n); return n; }|'
    ctl 'R7-C8 firmware binding never services executor' router \
      '/    radmin_service_once(target, exec);/d'
    ctl 'R7-C9 authenticated actor slot lost before real ACL handler' router \
      's|const LineExec r = exec_console_line(line, n, LineFormat::text, out, nullptr, 0, ctx);|CommandContext no_actor = ctx; no_actor.acl_slot = 0xFF; const LineExec r = exec_console_line(line, n, LineFormat::text, out, nullptr, 0, no_actor);|'
  fi
  ctl 'A7-C1 config loader floor lowered, same-size v25 loads' nvh \
    's|/\*v_min=\*/kVersionMinLoad, /\*v_max=\*/kVersion|/\*v_min=\*/25, /\*v_max=\*/kVersion|'
  ctl 'A7-C2 config loader floor raised, current v26 refuses' nvh \
    's|/\*v_min=\*/kVersionMinLoad, /\*v_max=\*/kVersion|/\*v_min=\*/27, /\*v_max=\*/kVersion|'
  ctl 'A10-C1 config loader reads the administration identity slot' nvh \
    's|    const int n = read_slot(kSlotCfg, \&out, sizeof out);|    const int n = read_slot(kSlotAdmid, \&out, sizeof out);|'

fi

if [ "${1:-}" != "--no-neg" ] && [ "$MR_PROBE_ARM" = accept ]; then
  s6_ctl 'R72-C1 control binding bypassed' 'return g_node.radmin_service_control();' \
    's|return g_node.radmin_service_control();|return false;|'
  s6_ctl 'R72-C2 open admission binding bypassed' 'return g_node.radmin_next_open(v);' \
    's|return g_node.radmin_next_open(v);|(void)v; return false;|'
  s6_ctl 'R72-C3 open sender binding bypassed' 'return g_node.radmin_send_open_frame();' \
    's|return g_node.radmin_send_open_frame();|return meshroute::RadminSend::none;|'
  s6_ctl 'R72-C4 open sink binding bypassed' 'g_node.radmin_open_append(i, p, n);' \
    's|g_node.radmin_open_append(i, p, n);|(void)i; (void)p; (void)n;|'
  s6_ctl 'R72-C5 open terminal binding changes outcome' 'g_node.radmin_open_complete(i, r);' \
    's|g_node.radmin_open_complete(i, r);|(void)r; g_node.radmin_open_complete(i, meshroute::RemoteTerminal::internal_error);|'
  for counter in inbound_refusal open_rate_refusal transcript_exhaustion response_enqueue_failure response_seal_failure; do
    s6_ctl "R72-C6 status $counter loses its measured value" "out.print(radmin.$counter);" \
      "s|out.print(radmin.$counter);|out.print(0);|"
  done
fi

if [ "${1:-}" != "--no-neg" ] && [ "$MR_PROBE_ARM" = client ]; then
  c8_ctl() {
    local label=$1 which=$2 needle=$3 script=$4 source="$FW_CMDS"
    [ "$which" = client ] && source="$ROOT/src/firmware_remote_client.cpp"
    if ! python3 -c 'import pathlib,sys; n=pathlib.Path(sys.argv[1]).read_text().count(sys.argv[2]); print("    source match count",n); sys.exit(0 if n==1 else 1)' "$source" "$needle"; then
      n_bad=$((n_bad+1)); echo "  FAIL $label source anchor not unique"; return
    fi
    ctl "$label" "$which" "$script"
  }
  c8_ctl 'C8-C1 short Print writes count as success' client 'written==n &&' 's|written==n \&\&|true \&\&|'
  c8_ctl 'C8-C2 console stage drops count as success' client 'drops_(ctx_)==before' 's|drops_(ctx_)==before|true|'
  c8_ctl 'C8-C3 JSON local sequences repeat zero' client 'EF_I("seq",seq)' 's|EF_I("seq",seq)|EF_I("seq",0)|'
  c8_ctl 'C8-C4 selected identity switches to self' client 'identity_from_seed(identity,seed.seed);' 's|identity_from_seed(identity,seed.seed);|identity=svc.self;|'
  c8_ctl 'C8-C5 expanded identity is not wiped' client 'SecretWipeGuard<Identity> guard{identity};' 's|SecretWipeGuard<Identity> guard{identity};||'
  c8_ctl 'C8-C6 exported seed is not wiped' client 'SecretWipeGuard<MgmtKeySeed> wipe{seed};' 's|SecretWipeGuard<MgmtKeySeed> wipe{seed};||'
  c8_ctl 'C8-C7 scheduled delay is omitted' client '(scheduled || detail)?snprintf' 's@(scheduled || detail)?snprintf@false?snprintf@'
  c8_ctl 'C8-C8 UTF8 carry is discarded' client 'utf8_tail_len_=static_cast<uint8_t>(total-end);' 's|utf8_tail_len_=static_cast<uint8_t>(total-end);|utf8_tail_len_=0;|'
  c8_ctl 'C8-C9 real router loses supplied transport' router 'const auto local=transport==CommandTransport::ble' 's|const auto local=transport==CommandTransport::ble|const auto local=false|'
  c8_ctl 'C8-C10 actual regen binding ignores live rows' router 'return meshroute::remote_client_busy(g_node.remote_client());' 's|return meshroute::remote_client_busy(g_node.remote_client());|return false;|'
  c8_ctl 'C8-C11 main-loop binding does not service controller' router 'meshroute::remote_client_service(g_node.remote_client(),static_cast<uint32_t>(g_hal.now()),' 's|    meshroute::remote_client_service(g_node.remote_client(),static_cast<uint32_t>(g_hal.now()),|    if (false) meshroute::remote_client_service(g_node.remote_client(),static_cast<uint32_t>(g_hal.now()),|'
  c8_ctl 'C8-C12 real key use binding ignores captured rows' router 'return meshroute::remote_client_key_in_use(g_node.remote_client(), slot);' 's|return meshroute::remote_client_key_in_use(g_node.remote_client(), slot);|return false;|'
  c8_ctl 'C8-C13 real target use binding ignores captured rows' router 'return meshroute::remote_client_target_in_use(g_node.remote_client(), slot);' 's|return meshroute::remote_client_target_in_use(g_node.remote_client(), slot);|return false;|'
  c8_ctl 'C8-C14 status request_table_pressure value lost' client 'out.print(c.request_table_pressure);' 's|out.print(c.request_table_pressure);|out.print(0);|'
  c8_ctl 'C8-C15 status assembly_failure value lost' client 'out.print(c.assembly_failure);' 's|out.print(c.assembly_failure);|out.print(0);|'
  c8_ctl 'C8-C16 status unmatched_response value lost' client 'out.print(c.unmatched_response);' 's|out.print(c.unmatched_response);|out.print(0);|'
  c8_ctl 'C8-C17 status auth_failure value lost' client 'out.print(c.auth_failure);' 's|out.print(c.auth_failure);|out.print(0);|'
  c8_ctl 'C8-C18 status local_result_pressure value lost' client 'out.print(c.local_result_pressure);' 's|out.print(c.local_result_pressure);|out.print(0);|'
  c8_ctl 'C8-C19 status radio_enqueue_failure value lost' client 'out.print(c.radio_enqueue_failure);' 's|out.print(c.radio_enqueue_failure);|out.print(0);|'
  c8_ctl 'C8-C22 real Node lookup ignores binding' client 'return static_cast<const Node*>(node)->id_bind_find_by_hash(hash);' 's|return static_cast<const Node\*>(node)->id_bind_find_by_hash(hash);|return -1;|'
  c8_ctl 'C8-C20 dormant ACK debt status omitted' client 'out.print(remote_client_ack_debt_count(s));' 's|out.print(remote_client_ack_debt_count(s));|out.print(0);|'
  c8_ctl 'C8-C21 observer bytes never reach delivery' client 'return n && write(t,line,n);' 's|return n \&\& write(t,line,n);|return n;|'
  c8_ctl 'C8-C22 disconnected BLE receives observations' client '(ble_ && ble_up_)' 's|(ble_ \&\& ble_up_)|(ble_ != nullptr)|'

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
