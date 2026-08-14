#!/usr/bin/env bash
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# §B105 FEATURE-LAYER PROBE — the first automated cover `src/firmware_ui.cpp` has ever had.
#
# WHY THIS EXISTS.
#   `src/firmware_ui.cpp` is compiled by NEITHER the native suite (`test_build_src = no`) NOR the simulator (which
#   compiles `lib/core`, never `src/`). [[B104]] recorded the consequence honestly: RENDER POLICY, the §5 MAC-IDLE
#   GATE, the 2 Hz THROTTLE and the BATTERY CADENCE had **no behavioural probe at all** — only source greps. Every
#   lifecycle defect in the UI-6/UI-7 arc (§B107 a frame that never repainted, §B115 a counter off by one from its
#   first tick, §B113 a state assigned zero times) was found by a human reading code or by the owner on metal.
#   ⇒ [[B105]] made the TU host-compilable — `fw_context_pure.h` + `DeviceHal::radio()` instead of `fw_context.h`
#     -> `<RadioLib.h>` — and THIS is what that refactor was for. The accessor without this probe is dead config.
#
# ★★ IT MUST REMAIN IN THE REPOSITORY AND BE COMMITTED WITH THIS SLICE, for the reason
#    `tools/probe_board_ui/run.sh` states at the same spot: this project has already LOST a proven 33-assert scenario
#    to a session scratchpad ([[meshroute-agent-scratchpad-is-volatile]]). A recipe in a note is not a storage
#    location.
#    ⛔ CORRECTED IN PLACE 2026-08-06: this line used to read "IT IS COMMITTED AND MUST STAY COMMITTED". That was NOT
#    the case at the time — the file was UNTRACKED, and under D4 the owner makes every commit.
#    ✅ CORRECTED AGAIN 2026-08-13 (§UI-7D slice B, which extends this probe): the 2026-08-06 correction has itself gone
#    stale — `git ls-files` now lists `tools/probe_firmware_ui/probe_main.cpp` and `run.sh`, so the file IS tracked and
#    this slice's edits appear as ordinary modifications. The obligation was always real; both claimed STATES were
#    point-in-time. ⇒ verify with `git ls-files`, never from this comment. (The standing lesson of the ledger's §3
#    incident, arriving from the other direction: a document asserting a state that has since changed.)
#
# USAGE:  tools/probe_firmware_ui/run.sh            # probe + NEGATIVE CONTROLS (the controls run BY DEFAULT)
#         tools/probe_firmware_ui/run.sh --no-neg   # probe only — NOT a gate, use only while iterating
# ⚠ The controls run by default DELIBERATELY. The sibling probe documented its controls as "not optional" while the
#   standard command skipped them, so the reported gate never included them (QA, 2026-08-04). Same trap, same answer.
#
# ★★★ WHAT A CONTROL HAS TO BE HERE. Each one applies ONE mutation to a COPY of `src/firmware_ui.cpp` — the tempting
#     WRONG FIX, not merely a deletion — and must make the probe RED. Three ways a control can be worthless, all three
#     checked and all three have fired in this arc:
#       1. the sed matched nothing        -> the copy is byte-identical  -> reported VACUOUS, counted as a failure;
#       2. the mutant does not compile    -> the probe never ran         -> reported, counted as a failure
#          (C0 is the ONE exception: it is the BUILD control and is REQUIRED to fail compilation);
#       3. the probe still passes         -> the check does not measure the property -> counted as a failure.
# ⚠ And the tree must come out untouched: the real sources' md5 is captured before and asserted after.

set -uo pipefail
cd "$(dirname "$0")" || exit 1
ROOT=$(cd ../.. && pwd)          # ★ absolute — a relative path in a cwd-resetting shell silently measured nothing
HERE=$(pwd)                      #   once already (register B82). Never make these relative.
BOARD="$ROOT/variants/heltec_v3"
FAKES="$ROOT/tools/probe_board_ui/fakes"   # ⓘ SHARED, not forked (U1) — the second probe adds Print/millis/Serial there
FW_UI="$ROOT/src/firmware_ui.cpp"
CXX=${CXX:-g++}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# ⚠ These -D MUST mirror `[env:heltec_v3]` (platformio.ini). If they drift, the probe measures a configuration the
#   board never builds — the same vacuous-instrument failure the controls exist to catch. `-DARDUINO` is what selects
#   the REAL `console_sink.h` staged console (MR_CONSOLE=1), so §B91's report line is a line this probe can assert.
DEFS=(-DARDUINO=100 -DMR_FEAT_OLED=1 -DMR_UI_BTN_PIN=0 -DMR_UI_TEAM_CHANNEL_ID=0 -DMR_CONSOLE=1 -DMR_N_LAYERS=2)
INCS=(-I"$FAKES" -I"$BOARD" -I"$ROOT/lib/hal" -I"$ROOT/lib/core" -I"$ROOT/lib/console" -I"$ROOT/src" -I"$ROOT/lib/monocypher/src")
STD=(-std=gnu++20 -fno-exceptions -fno-rtti -O0)

# ---- the tree must not move ------------------------------------------------------------------------------------
MD5_BEFORE=$(cat "$FW_UI" "$HERE/probe_main.cpp" "$FAKES/Arduino.h" "$ROOT/src/fw_context_pure.h" \
                 "$ROOT/lib/hal/device_hal.h" | md5sum | cut -d' ' -f1)

echo "== §B105 feature-layer probe (src/firmware_ui.cpp, host-compiled) =="

# ---- support archive: lib/core + lib/hal + lib/console + monocypher, built ONCE and reused by every variant ------
# ⓘ No -Werror on this half: `lib/core/node_hashlocate.cpp:1365` carries a PRE-EXISTING -Wmisleading-indentation that
#   this slice neither introduced nor is allowed to fix (C1: refactor XOR fix). The gate for lib/ warnings is
#   `tools/warning_census.sh` on the real board envs, not this probe.
echo "-- building the support archive (lib/core + lib/hal + lib/console + monocypher) ..."
SUP_SRCS=("$ROOT"/lib/core/*.cpp "$ROOT/lib/hal/device_hal.cpp" "$ROOT/lib/hal/timer_wheel.cpp"
          "$ROOT/lib/hal/airtime_ledger.cpp" "$ROOT/lib/console/console_json.cpp" "$ROOT/lib/console/console_parse.cpp")
sup_objs=()
for s in "${SUP_SRCS[@]}"; do
  o="$OUT/$(basename "${s%.*}").o"
  "$CXX" "${STD[@]}" -Wall -Wextra "${DEFS[@]}" "${INCS[@]}" -c "$s" -o "$o" 2>/dev/null || {
      echo "SUPPORT BUILD FAILED on $s"; exit 1; }
  sup_objs+=("$o")
done
"$CXX" -std=gnu17 -O0 "${DEFS[@]}" -I"$ROOT/lib/monocypher/src" -c "$ROOT/lib/monocypher/src/monocypher.c" \
       -o "$OUT/monocypher.o" 2>/dev/null || { echo "SUPPORT BUILD FAILED on monocypher"; exit 1; }
sup_objs+=("$OUT/monocypher.o")
ar rcs "$OUT/libsupport.a" "${sup_objs[@]}" || exit 1

# probe_main is the harness; it is built ONCE, with -Werror, because nothing mutates it.
"$CXX" "${STD[@]}" -Wall -Wextra -Werror "${DEFS[@]}" "${INCS[@]}" -c "$HERE/probe_main.cpp" -o "$OUT/probe_main.o" || {
    echo "PROBE HARNESS BUILD FAILED — see above"; exit 1; }

# build_variant(src, out_binary, extra_cxxflags...) -> 0 on a successful link
build_variant() {
  local src=$1 bin=$2; shift 2
  "$CXX" "${STD[@]}" -Wall -Wextra "$@" "${DEFS[@]}" "${INCS[@]}" -c "$src" -o "$OUT/fw_ui_var.o" 2>"$OUT/build.log" \
    && "$CXX" "$OUT/probe_main.o" "$OUT/fw_ui_var.o" "$OUT/libsupport.a" -o "$bin" 2>>"$OUT/build.log"
}

# ---- THE LIVE RUN. -Werror here: the shipped TU must be clean at the same warning bar the board builds hold it to.
if ! build_variant "$FW_UI" "$OUT/probe" -Werror; then
  echo "PROBE BUILD FAILED — src/firmware_ui.cpp did not host-compile:"; sed 's/^/    /' "$OUT/build.log" | head -20
  exit 1
fi
"$OUT/probe"; rc=$?
echo "probe exit=$rc"

# ---- ★★★ §UI-14: THE SECOND ARM OF SPEC §3.6.2's CONDITIONAL `BLE mode` ROW -------------------------------------
# The row is ABSENT when the UI-12 transport is not compiled — which is EVERY env in this tree — so the live run above
# measures only one half of the condition. ⛔ A conditional tested in one arm is a conditional whose other arm has
# never run. This builds BOTH TUs with `-DMR_UI_BLE_ROW=1` (the same flag the env that lands a transport will set) and
# requires the probe to pass THERE TOO; `probe_main.cpp` carries the matching `#if`, so one source asserts both arms.
echo
echo "== §UI-14 second arm: the BLE row's condition MET (-DMR_UI_BLE_ROW=1) =="
if "$CXX" "${STD[@]}" -Wall -Wextra -Werror -DMR_UI_BLE_ROW=1 "${DEFS[@]}" "${INCS[@]}" \
        -c "$HERE/probe_main.cpp" -o "$OUT/probe_main_ble.o" 2>"$OUT/ble.log" \
   && "$CXX" "${STD[@]}" -Wall -Wextra -Werror -DMR_UI_BLE_ROW=1 "${DEFS[@]}" "${INCS[@]}" \
        -c "$FW_UI" -o "$OUT/fw_ui_ble.o" 2>>"$OUT/ble.log" \
   && "$CXX" "$OUT/probe_main_ble.o" "$OUT/fw_ui_ble.o" "$OUT/libsupport.a" -o "$OUT/probe_ble" 2>>"$OUT/ble.log"; then
  if "$OUT/probe_ble" > "$OUT/probe_ble.out" 2>&1; then
    echo "  ok   the BLE-row arm builds and PASSES ($(grep -c '' "$OUT/probe_ble.out") lines)"
    grep -E "^[0-9]+ passed" "$OUT/probe_ble.out" | sed 's/^/       /'
  else
    echo "  FAIL the BLE-row arm is RED:"; grep '^  FAIL' "$OUT/probe_ble.out" | sed 's/^/    /' | head -10; rc=1
  fi
else
  echo "  FAIL the BLE-row arm did not build:"; sed 's/^/    /' "$OUT/ble.log" | head -10; rc=1
fi
# ★ THE DENOMINATOR FOR THE COVERAGE ROLL-UP AT THE BOTTOM, taken from the probe itself. `PROBE_LIST=1` makes every
#   CHK print its label whether it passed or not, so "N of M checks are reddened by a control" is MEASURED here rather
#   than maintained by hand in a comment — which is exactly how the header's "20 of 25" went stale in one slice.
PROBE_LIST=1 "$OUT/probe" 2>/dev/null | sed -n 's/^  CHECK //p' | sort -u > "$OUT/all_checks.txt"
: > "$OUT/reddened.txt"

# ---------------------------------------------------------------------------------------------------------------
# NEGATIVE CONTROLS
# ---------------------------------------------------------------------------------------------------------------
n_ctl=0; n_bad=0
# ctl(label, must_build, sed-script) — must_build=yes: the mutant has to compile AND the probe has to go red.
#                                      must_build=no : the mutant has to FAIL TO COMPILE (the build IS the check).
ctl() {
  local label=$1 must_build=$2 script=$3
  sed "$script" "$FW_UI" > "$OUT/mutant.cpp"
  if cmp -s "$FW_UI" "$OUT/mutant.cpp"; then
    n_bad=$((n_bad+1)); echo "  FAIL $label — the mutation changed NOTHING, so the control is VACUOUS"; return
  fi
  if [ "$must_build" = no ]; then
    if build_variant "$OUT/mutant.cpp" "$OUT/mutant.bin"; then
      n_bad=$((n_bad+1)); echo "  FAIL $label — it still BUILDS, so the property is not what this control claims"
    else
      n_ctl=$((n_ctl+1)); echo "  ok   $label (build fails, as required)"
    fi
    return
  fi
  if ! build_variant "$OUT/mutant.cpp" "$OUT/mutant.bin"; then
    n_bad=$((n_bad+1)); echo "  FAIL $label — the mutant does not COMPILE, so the probe never ran against it:"
    sed 's/^/        /' "$OUT/build.log" | head -6; return
  fi
  if "$OUT/mutant.bin" >"$OUT/mutant.out" 2>&1; then
    n_bad=$((n_bad+1)); echo "  FAIL $label — the probe still PASSES against the mutant (the check measures nothing)"
  else
    n_ctl=$((n_ctl+1)); echo "  ok   $label -> RED ($(grep -c '^  FAIL' "$OUT/mutant.out") check(s) failed)"
    # record WHICH checks this control reddened, for the roll-up (the CHK format is "  FAIL <label padded to 64>  <expr>")
    sed -n 's/^  FAIL \(.\{1,64\}\)  .*$/\1/p' "$OUT/mutant.out" | sed 's/[[:space:]]*$//' >> "$OUT/reddened.txt"
  fi
}

if [ "${1:-}" != "--no-neg" ]; then
  echo
  echo "== negative controls (each MUST make the probe red — or, for C0, must fail to build) =="

  # C0 ★★ THE SLICE'S OWN CONTROL. This whole probe exists because [[B105]] took `fw_context.h` out of the TU. Put it
  #    back and the host build dies at `<RadioLib.h>` — which is the measurement, stated as a control so that nobody
  #    can reintroduce the include and still believe the gate is green.
  ctl "C0  restoring #include \"fw_context.h\" breaks the host build" no \
      's|#include "fw_context_pure.h"|#include "fw_context.h"|'

  # C1-C3 THE §5 MAC-IDLE GATE. Three mutations, because the predicate has three wrong answers and each is tempting:
  #   drop it entirely ("the gate already knows"), keep only the radio ("the queue is drained by fw_main anyway"),
  #   keep only the queue ("tx_busy is what the queue implies"). A ~25 ms blocking I2C frame against a
  #   `cts_to_data_gap_ms` of 5 is the reason all three are wrong.
  ctl "C1  mac_idle() -> true (no gate at all)" yes \
      's|bool mac_idle() { return !g_hal.radio().tx_busy() \&\& g_hal.txq_depth() == 0; }|bool mac_idle() { return true; }|'
  ctl "C2  mac_idle() keeps only the radio clause" yes \
      's|bool mac_idle() { return !g_hal.radio().tx_busy() \&\& g_hal.txq_depth() == 0; }|bool mac_idle() { return !g_hal.radio().tx_busy(); }|'
  ctl "C3  mac_idle() keeps only the queue clause" yes \
      's|bool mac_idle() { return !g_hal.radio().tx_busy() \&\& g_hal.txq_depth() == 0; }|bool mac_idle() { return g_hal.txq_depth() == 0; }|'

  # C10 ★★ THE OTHER HALF OF C1-C3, AND IT IS THE §W10b LESSON APPLIED HERE. C1-C3 only prove that a gate exists;
  #     a `mac_idle()` stuck at FALSE satisfies every suppression clause they check while the panel never paints
  #     again. The "paint RESUMES" assertions are what forbid that, and without this control NOTHING measured them —
  #     a check whose only control is the permissive direction measures half the property.
  ctl "C10 mac_idle() -> false (suppressed for ever; the panel never paints)" yes \
      's|bool mac_idle() { return !g_hal.radio().tx_busy() \&\& g_hal.txq_depth() == 0; }|bool mac_idle() { return false; }|'

  # C11 THE PAGE-FEEDBACK LOOP. `FrameGate` learns a frame is COMPLETE only from the board's `next_page()` verdict.
  #     Advance the page without feeding the answer back and the gate believes a frame is open for ever: no later
  #     frame ever opens, unread counters are never marked read, and every native case stays green because none of
  #     them compile this file. (`tools/probe_board_ui/run.sh` W2 pins the same wiring STRUCTURALLY; this measures it.)
  ctl "C11 next_page()'s verdict is never fed back to the gate" yes \
      's|    s_gate.on_page(mrui::next_page(), s_model, s_counters);|    (void)mrui::next_page();|'

  # C4 RENDER POLICY — THE CALLER HALF OF ONCE-PER-PAGE, i.e. exactly the cover [[B104]] recorded as lost. The
  #    `next_page` arm pages the panel WITHOUT re-drawing, so 7 of 8 pages ship blank. U8g2 clips per page rather
  #    than accumulating, which is why this is a defect and not an optimisation.
  ctl "C4  pages advance WITHOUT re-drawing the scene (7 of 8 blank)" yes \
      's|        case mrui::FrameStep::next_page:|        case mrui::FrameStep::next_page: mrui::set_power_save(false); s_gate.on_page(mrui::next_page(), s_model, s_counters); return;|'

  # C5 THE THROTTLE, as integration. `FrameGate` owns the decision and the native suite drives it; what nothing but
  #    this probe can see is whether the tick OBEYS it. A forced repaint in the `idle` arm ("the panel looks stale")
  #    keeps every native case green.
  ctl "C5  the idle arm forces a repaint, bypassing the 2 Hz throttle" yes \
      's|        case mrui::FrameStep::idle:|        case mrui::FrameStep::idle: mrui::begin_frame();|'

  # C6-C8 THE BATTERY CADENCE. Three wrong answers, and the middle one is the documented near-miss: gating on a
  #    SUCCEEDED read re-samples for ever on a board whose reader answers "unavailable" — which is every V3 built
  #    today, because `battery_sample_mv()` is hardcoded -1 until Task 9.
  ctl "C6  the ADC burst is no longer gated on a MAC-idle radio" yes \
      's|    if (!mac_idle()) return;|    if (false) return;|'
  ctl "C7  the cadence gates on SUCCEEDED instead of ATTEMPTED" yes \
      's|    if (s_batt_attempted \&\& uint32_t(now_ms - s_batt_next_ms) >= (1u << 31)) return;|    if (s_batt_mv >= 0 \&\& uint32_t(now_ms - s_batt_next_ms) >= (1u << 31)) return;|'
  ctl "C8  the 30 s period is never re-armed (sample every pass)" yes \
      's|    s_batt_next_ms   = now_ms + kBattPeriodMs;|    s_batt_next_ms   = now_ms;|'

  # C9 §B91's report channel, inverted — the classic. A dead panel then stays silent and a live one cries wolf.
  ctl "C9  the dead-panel report tests board_init() the wrong way round" yes \
      's|    if (!mrui::board_init()) mrcon.println|    if (mrui::board_init()) mrcon.println|'

  # C12 the boot hook stops bringing the panel up at all ("drop the noisy boot line"). Without this, P0 asserted
  #     something no mutation could take away — and an unbreakable check is not a check.
  ctl "C12 mr_ui_init no longer brings the panel up" yes \
      's|    if (!mrui::board_init()) mrcon.println(F("!! OLED panel did not ACK (check Vext / addr 0x3C / wiring)"));|    ;|'

  # C13-C16 ★★ §UI-9 — WHAT THE CADENCE PUTS ON THE PANEL. C6-C8 pin WHEN the ADC is read; these pin what the reading
  #   becomes. All four are plausible edits that leave every native case and every C1-C12 control green.
  #   ⓘ C15 is the RULED render policy, not a preference: `3.9V` or `--`, never a percentage (plan Task 9 Step 3,
  #     spec §3.3). It is a control rather than a comment so the ruling cannot be undone quietly.
  ctl "C13 an unavailable read ERASES the last good value" yes \
      's|    if (mv >= 0) s_batt_mv = mv;|    s_batt_mv = mv;|'
  ctl "C14 the unavailable render invents a plausible voltage instead of --" yes \
      's|    if (mv < 0) { snprintf(out, cap, "--"); return; }|    if (mv < 0) { snprintf(out, cap, "3.9V"); return; }|'
  ctl "C15 the bar renders a PERCENTAGE instead of volts (ruled out)" yes \
      's|    snprintf(out, cap, "%u.%uV", unsigned(mv / 1000), unsigned((mv % 1000) / 100));|    snprintf(out, cap, "%u%%", unsigned(mv / 42));|'
  ctl "C16 the bar hardcodes a voltage instead of reading the model" yes \
      's|    char volts\[12\]; fmt_volts(volts, sizeof volts, s.batt_mv);|    char volts[12]; fmt_volts(volts, sizeof volts, 3900);|'

  # C17-C26 ★★★ §UI-7D slice B — THE DETAIL/DELETE MODAL'S DEVICE HALF. The model's half is under the native gate
  #   (32 mutations); these ten are the steps NO native case can reach, and the two identity ones are the shape
  #   [[B133]] already produced once: the DM and channel seq spaces are INDEPENDENT, so a lookup or an erase that drops
  #   the KIND deletes the other store's record with the same number. The probe records DM 1/2 and channel 1/2 for
  #   exactly that reason.
  ctl "C17 the record lookup matches seq only (the kind clause dropped)" yes \
      's|    if (e.kind != c->kind \|\| e.seq != c->seq) return true;|    if (e.seq != c->seq) return true;|'
  ctl "C18 the erase targets the DM store unconditionally (B133's shape)" yes \
      's|g_node.inbox().erase(rq.kind, rq.seq)|g_node.inbox().erase(mrui::InboxKind::dm, rq.seq)|'
  ctl "C19 the preview row carries msg_id where the identity should be" yes \
      's|^    r.seq        = e.seq;|    r.seq        = e.msg_id;|'
  ctl "C20 the preview row hardcodes the kind (every row reads DM)" yes \
      's|^    r.kind       = e.kind;|    r.kind       = MESHROUTE_NS::InboxKind::dm;|'
  # ⛔ C21 IS THE HARD REQUIREMENT: "a visual disappearance without durable success is forbidden" (spec §3.5). Report
  #    every erase as a success and the modal closes while the record is still there — the one failure this feature must
  #    not have. P6g reaches it because it evicts the open record out of band, so the confirmed erase really does come
  #    back `not_found`.
  ctl "C21 every erase is reported as a SUCCESS regardless of the outcome" yes \
      's|    s_model.on_inbox_erased(rq.kind, rq.seq, g_node.inbox().erase(rq.kind, rq.seq));|    (void)g_node.inbox().erase(rq.kind, rq.seq); s_model.on_inbox_erased(rq.kind, rq.seq, MESHROUTE_NS::InboxEraseResult::erased);|'
  # C27 the mirror of C18: the erase hits the CHANNEL store whatever the record was.
  ctl "C27 the erase targets the CHANNEL store unconditionally" yes \
      's|g_node.inbox().erase(rq.kind, rq.seq));|g_node.inbox().erase(mrui::InboxKind::channel, rq.seq));|'
  # C28 the switch arms crossed — OPENING a record deletes it. A copy-paste away, and the negative-space checks
  #     ("opening DELETED NOTHING") are what see it.
  ctl "C28 opening a record DELETES it (the switch arms crossed)" yes \
      's|        case mrui::InboxWhat::open:  ui_open_inbox_detail(rq, now_ms); break;|        case mrui::InboxWhat::open:  ui_erase_inbox_record(rq); break;|'
  ctl "C22 the tick never serves the inbox request (the modal cannot open)" yes \
      's|    ui_service_inbox_request(now_ms);|    ;|'
  ctl "C23 the body length is left for the model to measure (the strlen shape)" yes \
      's|s_model.on_inbox_opened(e.kind, e.seq, e.origin, e.channel_id, e.body, e.body_len, c->now_ms);|s_model.on_inbox_opened(e.kind, e.seq, e.origin, e.channel_id, e.body, 0, c->now_ms);|'
  ctl "C24 draw_frame stops giving the modal the body" yes \
      's|    if (st.detail != mrui::InboxModal::closed) { draw_inbox_detail(st); return; }|    ;|'
  # C25-C26 the REFUSAL's two halves on the list screen — the message, and the suppressed highlight (B64's rule
  #   one plane over: a `>` beside a record the model has refused to act on is the mis-delete in display form).
  ctl "C25 the MESSAGE GONE refusal row is dropped from the list" yes \
      's|    if (st.inbox_pick_gone) mrui::draw_text(0, body_y(kBodyRows - 1), "MESSAGE GONE");|    ;|'
  # C29-C36 ★★★ §UI-14 — THE SETTINGS SCREEN's DEVICE HALF. The model's half is under the native gate; these are the
  #   steps NO native case can reach, and C31 is THE named one: §3.6.1 forbade the marker from being `UiState::dirty`
  #   IN ADVANCE, and this file is where both are in scope at once.
  ctl "C29 the SETTINGS screen is never drawn (the switch arm dropped)" yes \
      's|        case mrui::Screen::settings: draw_settings_screen(st, s, c);  break;   // §UI-14|        case mrui::Screen::settings: break;|'
  ctl "C30 the model is never given the config service (the menu is inert)" yes \
      's|    s_model.attach_config(s_cfg);|    ;|'
  # ⛔⛔ C31 IS THE DEFECT §3.6.1 NAMED BEFORE IT COULD BE WRITTEN: `UiState::dirty` means "a repaint is owed" and is
  #    read three lines from the marker. Rendering it AS the marker leaves `CFG* UNSAVED` on a panel with nothing
  #    unsaved — P7c's "the marker is GONE once it is durable" is what sees it.
  ctl "C31 the draft marker renders UiState::dirty instead of config_unsaved()" yes \
      's|    v.unsaved  = s_cfg.config_unsaved();|    v.unsaved  = s_model.state().dirty;|'
  ctl "C32 the value row shows a default instead of the DRAFT" yes \
      's|    v.draft    = s_cfg.draft();|    v.draft    = mrfw::CfgValues{};|'
  ctl "C33 STATUS drops the draft marker (it is only on SETTINGS)" yes \
      's|    if (marker\[0\]) { snprintf(l, sizeof l, "STATUS %s", marker); mrui::draw_text(0, body_y(0), l); }|    if (false) { }|'
  ctl "C34 the editor is indistinguishable from the browsing row (no bracket)" yes \
      's|            if (ed) snprintf(l, sizeof l, "%c%-10s \[%s\]", here ? '"'"'>'"'"' : '"'"' '"'"', mrui::settings_row_label(r), v);|            if (false) { }|'
  ctl "C35 RESTART NEEDED never reaches STATUS (the reboot fact is dropped)" yes \
      's|    if (c.reboot) { mrui::draw_text(0, body_y(4), mrui::kCfgRestartText); return; }|    ;|'
  # ⛔ C36 is the CONDITIONAL ROW's own control: rendering it unconditionally offers a setting this build cannot act on.
  ctl "C36 the BLE row is rendered unconditionally (the transport condition ignored)" yes \
      's|    s.ble_row    = (MR_UI_BLE_ROW != 0);|    s.ble_row    = true;|'

  # C37-C40 ★★★ §UI-14 follow-up — `mr_ui_on_config_saved`, the IMMEDIATE conflict notification §3.6.1 requires. The
  #   hook's four obligations each have a wrong answer, and C38 is the one that would ship green everywhere else: the
  #   latch raised WITHOUT a repaint is true and invisible, because `FrameGate::step` returns `idle` on a clean model.
  ctl "C37 the hook never tells the service (the notification is dropped)" yes \
      's|    s_cfg.note_external_write(b);|    (void)b;|'
  ctl "C38 the latch is raised but no repaint is asked for (true and INVISIBLE)" yes \
      's|    if (s_cfg.conflict() != was) s_model.mark_dirty();|    (void)was;|'
  ctl "C39 the is_open() guard is dropped (flash is read on every cfg set)" yes \
      's|    if (!s_cfg.is_open()) return;|    ;|'
  ctl "C40 an unreadable record is treated as a conflict (a latch from no evidence)" yes \
      's|    if (!mrfw::device_cfg_store().load(b)) return;|    (void)mrfw::device_cfg_store().load(b);|'

  # C41-C44 ★★★ §T3 — THE THREE CHANGED STRINGS AND THE TWO NEW STATES. Each control is the TEMPTING WRONG ANSWER,
  #   not a deletion: C41 puts `SENT` back on core acceptance (the §B69 false confirmation this slice removes), C42
  #   makes the EARNED state say the unearned word, C43 restores `SENT, no relay` on a state that never established
  #   any airing, and C44 collapses the two DM states so the promotion is invisible.
  # ⚠ C41/C42 are DIRECTIONAL OPPOSITES on purpose: with only one of them a renderer that printed the SAME string for
  #   both states would still redden something, and the pair is what proves the two states are distinguishable.
  ctl "C41 ChanState::waiting says SENT again (the pre-T3 false confirmation)" yes \
      's|case mrui::ChanState::waiting:    mrui::draw_text(0, body_y(1), "QUEUED");|case mrui::ChanState::waiting:    mrui::draw_text(0, body_y(1), "SENT, waiting");|'
  ctl "C42 ChanState::aired says QUEUED (the earned state never shows)" yes \
      's|case mrui::ChanState::aired:      mrui::draw_text(0, body_y(1), "SENT, waiting");|case mrui::ChanState::aired:      mrui::draw_text(0, body_y(1), "QUEUED");|'
  ctl "C43 the no-relay outcome reads SENT, no relay again" yes \
      's|"NO RELAY HEARD"|"SENT, no relay"|'
  ctl "C44 DmState::aired_waiting is rendered as QUEUED too" yes \
      's|case mrui::DmState::aired_waiting: mrui::draw_text(0, body_y(1), "SENT, waiting");|case mrui::DmState::aired_waiting: mrui::draw_text(0, body_y(1), "QUEUED");|'

  ctl "C26 the highlight is NOT suppressed while the refusal stands" yes \
      's|                 (!st.inbox_pick_gone \&\& first + row == st.cursor) ? '"'"'>'"'"' : '"'"' '"'"', tag, e.text, age);|                 (first + row == st.cursor) ? '"'"'>'"'"' : '"'"' '"'"', tag, e.text, age);|'
fi

# ---- ★★ §T3 (design P6) — THE OLD STRING MUST BE GONE FROM EVERY RENDERING SOURCE ------------------------------
# ⛔ SCOPE IS `src/` + `tools/`, DELIBERATELY, AND NOT TREE-WIDE. The bench guide and the bug register legitimately
#   QUOTE `SENT, no relay` inside withdrawn-wording blocks (a withdrawal is kept visible, never deleted), so a
#   tree-wide grep would force those histories to be erased to make a gate green — the wrong trade. What must be true
#   is that nothing a panel can PRINT still carries it.
echo
echo "== §T3 P6: the retired string is absent from every rendering source =="
# ⚠ COMMENTS ARE STRIPPED FIRST (§B77's lesson, borrowed from `probe_board_ui`'s `code_flat`): the rename is
#   RECORDED in a comment beside the arm it changed, and a bare grep matches the note that says the string is gone.
#   What must be absent is a string a panel can PRINT, i.e. code.
: > "$OUT/p6.txt"
while IFS= read -r f; do
  sed 's://.*::' "$f" | grep -n -F 'SENT, no relay' | sed "s|^|$f:|" >> "$OUT/p6.txt"
done < <(find "$ROOT/src" "$ROOT/tools" \( -name '*.cpp' -o -name '*.h' \) )
if [ -s "$OUT/p6.txt" ]; then
  echo "  FAIL 'SENT, no relay' still present in a rendering source:"; sed 's/^/    /' "$OUT/p6.txt" | head -10; rc=1
else
  echo "  ok   'SENT, no relay' appears in no src/ or tools/ source"
fi
# ⚠ AND THE VACUITY GUARD FOR IT (the grep must be able to FIND things): the string that REPLACED it must be present.
if grep -rq --include=*.cpp -F 'NO RELAY HEARD' "$ROOT/src"; then
  echo "  ok   ...and its replacement IS present, so the grep is not matching nothing"
else
  echo "  FAIL the replacement string is absent too — the check above proved nothing"; rc=1
fi

# ---- the tree must be exactly as we found it -------------------------------------------------------------------
MD5_AFTER=$(cat "$FW_UI" "$HERE/probe_main.cpp" "$FAKES/Arduino.h" "$ROOT/src/fw_context_pure.h" \
                "$ROOT/lib/hal/device_hal.h" | md5sum | cut -d' ' -f1)
if [ "$MD5_BEFORE" != "$MD5_AFTER" ]; then
  echo "  FAIL the probe MODIFIED a real source file ($MD5_BEFORE -> $MD5_AFTER)"; rc=1
else
  echo
  echo "sources unchanged: md5 $MD5_AFTER"
fi

# ---- ★★ THE COVERAGE ROLL-UP: which checks can a control actually break? ----------------------------------------
# A green probe with green controls still says nothing about the checks NO control touches. This prints the ratio and
# NAMES the exceptions, so an un-reddened check has to be justified in the source rather than assumed covered.
if [ -s "$OUT/all_checks.txt" ] && [ "${1:-}" != "--no-neg" ]; then
  sort -u "$OUT/reddened.txt" > "$OUT/reddened_u.txt"
  n_all=$(grep -c . "$OUT/all_checks.txt"); n_red=$(grep -c . "$OUT/reddened_u.txt")
  echo
  echo "coverage: $n_red of $n_all checks are reddened by at least one control"
  comm -23 "$OUT/all_checks.txt" "$OUT/reddened_u.txt" | sed 's/^/  (no control reddens) /'
elif [ "${1:-}" != "--no-neg" ]; then
  echo "  FAIL the check roll-up produced 0 labels — PROBE_LIST is not wired, so the ratio would be vacuous"; rc=1
fi

echo "controls: $n_ctl verified / $n_bad unusable"
[ "$n_bad" -eq 0 ] || rc=1
if [ "$rc" -eq 0 ]; then echo "PASS"; else echo "FAIL"; fi
exit $rc
