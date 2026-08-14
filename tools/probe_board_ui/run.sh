#!/usr/bin/env bash
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# §UI-5 board-canvas probe — the ONLY automated cover `variants/heltec_v3/board_ui.cpp` will ever have.
#
# WHY THIS EXISTS AND WHY IT IS NOT A `pio` ENV:
#   `board_ui.cpp` is compiled by NEITHER the native suite (`test/` is native-only and has no Arduino/U8g2) NOR the
#   simulator (which never compiles `src/` or `variants/`). So the two behaviours that matter most are unreachable by
#   every automated gate this project has:
#     • EDGE-TRIGGERED blanking — `set_power_save(1)` exactly once on the transition, never per tick. A per-tick call
#       means continuous I2C traffic from a panel that is supposed to be OFF.
#     • PAGE-CHUNKED rendering — a full 1024 B frame at 400 kHz is ~25 ms of blocking I2C, against a
#       `cts_to_data_gap_ms` of 5 and 5-8 ms turnarounds. A full-frame repaint DROPS RADIO FRAMES.
#   ⇒ this probe host-compiles the REAL `board_ui.cpp` against COUNTING SHIMS for Arduino and U8g2 (`fakes/`), so those
#   behaviours become assertable without a board.
#
# ★★ IT IS COMMITTED, DELIBERATELY, AND MUST STAY COMMITTED. It lived in a session scratchpad first; this project has
#    already LOST a proven 33-assert scenario that way (see MEMORY / `meshroute-agent-scratchpad-is-volatile`). A
#    reconstruction recipe in a note is not a storage location. Owner-ruled 2026-08-04.
#
# USAGE:  tools/probe_board_ui/run.sh            # probe + NEGATIVE CONTROLS (the controls run BY DEFAULT)
#         tools/probe_board_ui/run.sh --no-neg   # probe only -- NOT a gate, use only while iterating
# ⚠ The controls run by default DELIBERATELY: the previous version documented them as "not optional" while the
#   standard command skipped them, so the reported gate never included them (QA, 2026-08-04).
#
# ⚠ The negative controls are the point: each reverts ONE behaviour in a COPY of `board_ui.cpp` and must turn the probe
#   red. A probe that passes against a broken copy is measuring nothing — that is the failure mode this project keeps
#   finding, so the controls are not optional.

set -uo pipefail
cd "$(dirname "$0")" || exit 1
ROOT=$(cd ../.. && pwd)                 # ★ absolute — a relative path in a cwd-resetting shell silently measured
                                        #   nothing once already (register B82). Never make these relative.
HERE=$(pwd)
BOARD="$ROOT/variants/heltec_v3"
CXX=${CXX:-g++}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# ⚠ These -D MUST mirror the `[env:heltec_v3]` SECTION of platformio.ini (addressed by section, never by line number:
#   the old form cited `:221`/`:227` and those had already drifted). If they drift, the probe measures a configuration
#   the board never builds — the same vacuous-instrument failure the controls exist to catch.
# ★ §UI-9 added MR_UI_ADC_CTRL / MR_UI_VBAT_READ. They are not decoration here: board_ui.cpp `#error`s without them
#   (C2), so a drift in EITHER direction — dropped from the env, or dropped from here — is a hard build failure rather
#   than a silently different measurement. THE DEFINED SET IS ALSO ASSERTED AGAINST platformio.ini by check S6 below.
BOARD_DEFS=(-DMR_FEAT_OLED=1 -DMR_UI_BTN_PIN=0 -DMR_UI_ADC_CTRL=37 -DMR_UI_VBAT_READ=1)
build() {   # build($1 = board_ui.cpp path, $2 = output binary)
  "$CXX" -std=gnu++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror \
     "${BOARD_DEFS[@]}" \
     -I"$HERE/fakes" -I"$BOARD" -I"$ROOT/lib/hal" -I"$ROOT/lib/core" -I"$ROOT/src" \
     "$HERE/probe_main.cpp" "$1" -o "$2" 2>&1
}

echo "== §UI-5 board-canvas probe =="
if ! build "$BOARD/board_ui.cpp" "$OUT/probe"; then
  echo "PROBE BUILD FAILED — see above"; exit 1
fi
"$OUT/probe"; rc=$?
echo "probe exit=$rc"

# ---------------------------------------------------------------------------------------------------------------------
# §UI-6 STRUCTURAL CHECKS. ⚠ WEAKER THAN THE PROBE, AND LABELLED AS SUCH: these read source/symbols instead of measuring
# behaviour. They exist because Task 6 moved three things OUT of host-compilable reach (src/firmware_ui.cpp includes
# fw_context.h, i.e. RadioLib and the whole device stack), and "no cover at all" is worse than a check that can at least
# turn red when the property alone is reverted. S1 is the strongest — it is a symbol-table fact, not a grep.
FW_UI="$ROOT/src/firmware_ui.cpp"
BOARD_H="$BOARD/board_ui.h"
s_pass=0; s_fail=0
schk() {  # schk(label, expr-as-command)
  if eval "$2" >/dev/null 2>&1; then s_pass=$((s_pass+1))
  else s_fail=$((s_fail+1)); echo "  FAIL $1"; fi
}
echo
echo "== §UI-6 structural checks (source/symbol level — weaker than the probe, deliberately) =="
[ -f "$FW_UI" ] || { echo "  FAIL S0 src/firmware_ui.cpp is missing"; rc=1; }

# S1 ★ THE DUPLICATE-SYMBOL TRIPWIRE. Task 6 took ownership of the three mr_ui_* hooks; UI-5 had defined them in the
#    board TU as TEMPORARY. Defining them in BOTH is a link failure on three shipped envs, and this is what catches a
#    re-add. Measured with nm on the real object, not by grepping for a comment.
"$CXX" -std=gnu++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror \
   "${BOARD_DEFS[@]}" \
   -I"$HERE/fakes" -I"$BOARD" -I"$ROOT/lib/hal" -I"$ROOT/lib/core" -I"$ROOT/src" \
   -c "$BOARD/board_ui.cpp" -o "$OUT/board_ui.o" 2>/dev/null
if [ -f "$OUT/board_ui.o" ]; then
  defined=$(nm --defined-only "$OUT/board_ui.o" 2>/dev/null | grep -c 'mr_ui_')
  schk "S1 board_ui.o defines NO mr_ui_* symbol (got $defined)" "[ '$defined' -eq 0 ]"
else
  s_fail=$((s_fail+1)); echo "  FAIL S1 could not compile board_ui.cpp to an object"
fi
# S2 ...and the feature layer defines all three, so fw_main's unconditional calls still link.
for h in mr_ui_init mr_ui_tick mr_ui_on_push; do
  schk "S2 src/firmware_ui.cpp defines $h" "grep -qE '^void $h\\(' '$FW_UI'"
done
# S3 THE ONCE-PER-PAGE REDRAW, caller half. U8g2 CLIPS each page rather than accumulating, so drawing once at frame
#    start and then only advancing pages leaves 7 of 8 pages blank (spec §5).
# ★ RE-EXPRESSED 2026-08-05 by §B107, and the property got STRONGER, not weaker. It used to check for >=2 `draw_frame`
#   call sites — one per branch — which only ever measured that somebody had remembered to duplicate the call. The
#   `open` and `next_page` arms now SHARE a single tail, so "drawn on every page" is structural: there is exactly ONE
#   draw site and it is unconditional once the switch has run. ⇒ check for exactly one of each of the three, which is
#   what makes a re-branching of the tail visible.
for pat in 'draw_frame(s_frame_state' 'mrui::begin_frame()' 's_gate.on_page(mrui::next_page(), s_model, s_counters)'; do
  schk "S3 exactly one '$pat' in the tick (got $(grep -cF "$pat" "$FW_UI"))" \
       "[ \"\$(grep -cF '$pat' '$FW_UI')\" -eq 1 ]"
done
# S4 THE HARD BOUNDARY, both directions. board_ui.h must not include the model; firmware_ui.cpp must not name the driver.
# ⚠⚠ BOTH OF THESE FAILED ON THEIR FIRST WRITING, AND FOR THE §B77 REASON: a bare grep for the forbidden name also
#    matches the COMMENT THAT FORBIDS IT — both files state the invariant in prose, so the check was reading its own
#    documentation as a violation. ⇒ strip `//` comments first (`code_of`), then grep CODE. Same class as the anchor
#    table that matched the sentence prescribing the anchor.
code_of() { sed 's://.*::' "$1"; }
schk "S4a board_ui.h #includes no firmware_ui_model.h" \
     "! code_of '$BOARD_H' | grep -qE '^[[:space:]]*#[[:space:]]*include.*firmware_ui_model'"
schk "S4b firmware_ui.cpp CODE names no U8g2/Wire/GPIO symbol" \
     "! code_of '$FW_UI' | grep -qE 'U8g2|U8G2|\\bWire\\b|pinMode\\(|digitalRead\\(|analogRead\\('"
# S5 §B91's report channel actually exists on the caller side (the probe cannot see it).
schk "S5 firmware_ui.cpp surfaces a failed board_init()" "grep -q 'if (!mrui::board_init())' '$FW_UI'"

# S6 ★★ §UI-9 — THE PROBE'S -D SET IS ASSERTED AGAINST THE ENV IT CLAIMS TO MIRROR, instead of a comment asking the
#    next reader to keep them in step. The header of `build()` has carried that request since UI-5 and its line
#    references had ALREADY drifted. A probe configured differently from the board measures a build nobody ships.
# ⚠ Addressed by SECTION, never by line number, and `;` comments are stripped FIRST — platformio.ini documents these
#   very macros in prose right beside them, which is the §B77 trap (a grep counting the comment that names the thing).
# ⚠ The count is required to be exactly 1 and is PRINTED: a pattern that matched nothing would otherwise compare
#   empty-to-empty and pass, which is the vacuous-comparison failure this file keeps finding.
env_defs() {   # the LIVE -DNAME=VALUE tokens of [env:heltec_v3]
  awk '/^\[env:heltec_v3\]/{on=1;next} /^\[/{on=0} on' "$ROOT/platformio.ini" \
    | sed 's/;.*//' | grep -oE '\-D[A-Za-z_][A-Za-z_0-9]*=[^[:space:]]+'
}
for nm in MR_UI_BTN_PIN MR_UI_ADC_CTRL MR_UI_VBAT_READ; do
  n_env=$(env_defs | grep -cE "^-D${nm}=")
  v_env=$(env_defs | grep -E "^-D${nm}=" | sed "s/^-D${nm}=//")
  v_prb=$(printf '%s\n' "${BOARD_DEFS[@]}" | grep -E "^-D${nm}=" | sed "s/^-D${nm}=//")
  schk "S6 -D${nm} mirrors [env:heltec_v3] (env x$n_env='${v_env:-<none>}' probe='${v_prb:-<none>}')" \
       "[ '$n_env' -eq 1 ] && [ -n '$v_prb' ] && [ '$v_env' = '$v_prb' ]"
done

# ---------------------------------------------------------------------------------------------------------------------
# ★★ THE WIRING CHECKS, AND EACH ONE CARRIES ITS OWN NEGATIVE CONTROL.
# The §B103/§B107/§B108 fixes are pure functions in `firmware_ui_send.h` / `firmware_ui_model.h`, so their LOGIC is
# driven by the native suite and turns red on a revert. What no native test can see is whether `firmware_ui.cpp`
# actually CALLS them — a fix wired to nothing passes every native case. That gap is exactly §B97 (four "required
# integration regressions" that hand-replicated the wiring they guarded and so could not have failed).
# ⇒ every check below is run on the real file, where it must PASS, and then once PER CONTROL on a copy with that single
#   property reverted, where it must FAIL. A structural check without its control measures nothing — and §B115's NO-GO
#   added the corollary: a check whose only control is a DELETION measures half the property, because a
#   plausible-but-wrong REPLACEMENT still passes it. See W10b, which is that lesson.
code_flat() { sed 's://.*::' "$1" | tr '\n' ' '; }   # §B77: strip comments first — a bare grep matches the comment
                                                     # that FORBIDS the thing, and both S4 checks failed that way once.
w_pass=0; w_fail=0; w_ctl=0
# ★★ MORE THAN ONE CONTROL PER CHECK IS ALLOWED, AND §B115's LAST MILE PROVED WHY IT IS NEEDED (independent QA,
#    2026-08-05). A single control almost always mutates by DELETION, and a check that only sees a deletion is exactly
#    how the population line at `freeze_outcome` stayed unguarded while W10 was green: replacing it with a
#    plausible-but-wrong value satisfied every clause the harness had. ⇒ pass as many sed scripts as the property has
#    wrong answers; EVERY one must be non-vacuous AND must turn the predicate red, and the count is reported.
# ★ GENERALISED 2026-08-13 (§UI-14 follow-up) — the file is now a PARAMETER, because the newest wiring property lives
#   in `src/firmware_config.cpp` (the config verb must NOTIFY the panel) rather than in `firmware_ui.cpp`. ⛔ The body
#   is NOT forked: `wchk` delegates, so all thirteen existing call sites are untouched and there is still exactly ONE
#   implementation of "PASS live, FAIL on every revert" (U1 — a second copy is how two harnesses drift).
wchk_in() {  # wchk_in(file, label, predicate-fn, sed-script-that-reverts-the-property [, further sed scripts ...])
  local file=$1 label=$2 pred=$3; shift 3
  if ! "$pred" "$file"; then w_fail=$((w_fail+1)); echo "  FAIL $label (not wired in $(basename "$file"))"; return; fi
  local n=0 revert
  for revert in "$@"; do
    n=$((n+1))
    sed "$revert" "$file" > "$OUT/fw_ui_revert.cpp"
    if cmp -s "$file" "$OUT/fw_ui_revert.cpp"; then
      w_fail=$((w_fail+1)); echo "  FAIL $label CONTROL $n — the revert changed NOTHING, so the check is vacuous"; return
    fi
    if "$pred" "$OUT/fw_ui_revert.cpp"; then
      w_fail=$((w_fail+1)); echo "  FAIL $label CONTROL $n — the check still passes against the reverted copy"; return
    fi
    w_ctl=$((w_ctl+1))
  done
  w_pass=$((w_pass+1))
}
wchk() { wchk_in "$FW_UI" "$@"; }   # the thirteen §UI-6/§UI-7 checks, unchanged: they all read firmware_ui.cpp
echo
echo "== §UI-6 wiring checks + their negative controls (each must PASS live and FAIL reverted) =="
# W1 §B103/F4 — the distress-reply scope. The shipped guard was `pu.channel_id == MR_UI_TEAM_CHANNEL_ID` alone, which
#    let any plaintext channel-0 post from any node in range render as "someone answered my distress call".
w1() { code_flat "$1" | grep -qE 'ui_route_recv_push\([^;]*g_node\.same_team\(pu\.team_id\)'; }
wchk "W1 mr_ui_on_push passes g_node.same_team(pu.team_id) to the recv router" \
     w1 's/g_node\.same_team(pu\.team_id)/true/'
# W2 §B107/F1 — the render lifecycle is DELEGATED, and the page feedback closes the loop. `FrameGate` decides; the
#    board reports how many pages are left. A tick that never feeds `next_page()` back leaves the gate believing a
#    frame is open for ever, which no native test can see because no native test calls this file.
w2() { code_flat "$1" | grep -qE 's_gate\.step\(s_model, s, mac_idle\(\)\)' && \
       code_flat "$1" | grep -qF 's_gate.on_page(mrui::next_page(), s_model, s_counters)'; }
wchk "W2 the tick delegates to FrameGate::step and feeds next_page() back" \
     w2 's/s_gate\.on_page(mrui::next_page(), s_model, s_counters)/(void)mrui::next_page()/'
# W3 §B107/F1, NEGATIVE SPACE — and this is the one that actually pins the fix. `dirty` is consumed AT THE FREEZE,
#    inside `FrameGate`. If this file clears it too, the bug is back regardless of what the gate does, and every
#    native case still passes. ⇒ the string must not appear here at all.
w3() { ! code_flat "$1" | grep -qF 'clear_dirty'; }
wchk "W3 firmware_ui.cpp never clears dirty itself (the freeze owns it)" \
     w3 's/s_gate\.on_page(mrui::next_page(), s_model, s_counters);/s_model.clear_dirty(); s_gate.on_page(mrui::next_page(), s_model, s_counters);/'
# W4 §B108/F2, NEGATIVE SPACE — the twin of W3. Arrivals are marked read by `FrameGate::on_page`, once a COMPLETE and
#    VISIBLE Inbox frame has gone out, by advancing the watermark to the serial that frame FROZE. If this file moves
#    any of that itself the shipped defect is back verbatim, and every native case still passes because none call it.
# ★ WIDENED 2026-08-05 (§B108 round 2) BECAUSE THE SIGNATURE IT TARGETED MOVED. `unread_dm`/`unread_ch` are no longer
#   fields — they are derived accessors — so the old pattern `s_counters\.unread_(dm|ch) *=` had become unable to
#   match anything this file could plausibly write, i.e. it would have kept passing while guarding nothing. The live
#   write surface is now `arr_*` / `read_*`. ⇒ forbid the file from NAMING any of the three at all: its only
#   legitimate access to them is the `publish()` in W5, and the recency stamps it does read are different fields.
w4() { ! code_flat "$1" | grep -qE 's_counters\.(unread|arr|read)_(dm|ch)'; }
wchk "W4 firmware_ui.cpp never moves the unread counters or watermarks itself (a completed frame does)" \
     w4 's/s_gate\.on_page(mrui::next_page(), s_model, s_counters);/s_counters.read_dm = s_counters.arr_dm; s_gate.on_page(mrui::next_page(), s_model, s_counters);/'
# W5 §B108 round 2, POSITIVE — the frame's DISPLAY counts and the ARRIVAL SERIALS it freezes must come from the ONE
#    `publish` conversion path (U2). Rebuilt field-by-field here they could be sampled at different instants, and the
#    frame would then mark read an arrival its own rendered number never included — B108's harm, one layer down.
w5() { code_flat "$1" | grep -qE 's_counters\.publish\(s\)'; }
wchk "W5 build_snapshot uses the single UiInboxCounters::publish conversion path" \
     w5 's/s_counters\.publish(s)/s.unread_dm = 0/'
# W6 §R1 (owner-ruled 2026-08-05) — THE LAST MILE OF THE WAKE, and it is the one step no native test can reach.
#    `UiModel::on_reply` clears `blanked` and `FrameGate::step` then stops answering `blank` — both natively driven.
#    What happens next is HERE: the step must be mapped onto the panel's two commands. If any awake arm stops calling
#    `set_power_save(false)`, the reply un-blanks the MODEL and the SSD1306 stays off — the exact harm R1 fixes,
#    surviving one layer down, with every native case still green.
# ★ All four arms are checked, not just one: `blank` -> true and idle/open/next_page -> false. `mac_busy` deliberately
#   touches neither (spec §5 rule 1), so it has no clause and must not grow one.
w6() { code_flat "$1" | grep -qE 'FrameStep::blank:[[:space:]]*mrui::set_power_save\(true\);' || return 1
       for arm in idle open next_page; do
         code_flat "$1" | grep -qE "FrameStep::$arm:[[:space:]]*mrui::set_power_save\\(false\\);" || return 1
       done
       return 0; }
wchk "W6 the tick maps FrameStep onto the panel: blank->power_save(1), every awake arm->power_save(0)" \
     w6 's/mrui::set_power_save(false)/(void)0/g'
# W7 §4.1 (UI-7) — THE LAST MILE OF THE CONDITIONAL `-l`, and it is the one step no native test can reach. The pure
#    composer is driven natively and turns red if it stops honouring `have_fix`; what NO native case can see is
#    whether this file computes that bit at all. Passing a literal `true` would compile, keep every native case green,
#    and make a fix-less node send `-t -l` — which `node.cpp:1553` REFUSES OUTRIGHT, so the distress call becomes NO
#    CALL. ⇒ the tick must reach the pure driver through the real predicate AND the real executor.
w7() { code_flat "$1" | grep -qE 'mrui::ui_perform_send\([^;]*ui_have_fix\(\), ui_exec' && \
       code_flat "$1" | grep -qE 'cfg\.lat_e7 != 0 \|\| cfg\.lon_e7 != 0'; }
wchk "W7 the tick reaches mrui::ui_perform_send through ui_have_fix() and the real executor" \
     w7 's/ui_have_fix(), ui_exec/true, ui_exec/'
# W8 UI-7, NEGATIVE SPACE — the twin of W3/W4. Line COMPOSITION lives in `firmware_ui_send.h`, where the native suite
#    asserts the issued command byte-for-byte. If this file ever builds a `send`/`send_channel` line itself, that
#    assertion is guarding a string nothing sends, and the conditional `-l` / the `back`-row refusal are bypassed —
#    with every native case still green. ⇒ the verbs must not appear in this file's CODE at all.
w8() { ! code_flat "$1" | grep -qE '"send(_channel)? '; }
wchk "W8 firmware_ui.cpp composes no send line itself (the pure composer owns the wire text)" \
     w8 's|mrui::ui_perform_send(s_tracker_emg|char bad[64]; snprintf(bad, sizeof bad, "send_channel %u x", 0u); (void)bad; mrui::ui_perform_send(s_tracker_emg|'
# W9 §B64 (owner-ruled 2026-08-05) — THE LAST MILE OF THE REFUSAL, and it is the one step no native test can reach. The
#    model's half is natively driven: `activate` refuses and `UiState::team_pick_gone` records it. What happens next is
#    HERE, and there are TWO obligations, not one:
#      (a) the reason is SAYABLE (C2) — one body row is reserved and the string is drawn; and
#      (b) the `>` HIGHLIGHT IS SUPPRESSED. Without (b) the panel names a target the model has already refused to use —
#          the mis-send surviving one layer down in display form, with every native case still green. That is precisely
#          the §B97 shape this whole W-block exists for.
# ⚠ The revert targets (b) alone, because (b) is the half that is a SAFETY property rather than a message.
w9() { code_flat "$1" | grep -qE 'st\.team_pick_gone \? uint8_t\(kBodyRows - 1\)' && \
       code_flat "$1" | grep -qE '!st\.team_pick_gone && first \+ row == st\.cursor' && \
       code_flat "$1" | grep -qF 'st.team_pick_gone) mrui::draw_text' && \
       code_flat "$1" | grep -qF 'TEAMMATE GONE, repick'; }
wchk "W9 the TEAM screen says B64's refusal AND suppresses the highlight while it stands" \
     w9 's/!st\.team_pick_gone && first + row == st\.cursor/first + row == st.cursor/'
# W10 §B115 (owner-MEASURED on metal 2026-08-05) — THE LAST MILE OF THE ATTEMPT COUNTER, and it is the one step no
#     native test can reach. The model's ordinal and the one formatter are both natively driven (`ui7-b115: …` cases,
#     the whole `1 of 3` -> `2 of 3` -> `3 of 3` sequence asserted as BYTES). What happens next is HERE, and this file
#     is where the defect actually lived: the FIRING arm rendered `v.tries + 1` — an UNCONDITIONAL `+1` on a counter
#     that had already counted the in-flight attempt — so three posts on the wire displayed `2 of 3` / `3 of 3` /
#     `4 of 3` and `1 of 3` was NEVER shown. Every native case was green over it, because none of them compile this file.
# ★ TWO CLAUSES, POSITIVE AND NEGATIVE SPACE, because either alone can be satisfied by the bug:
#     (a) the arm must reach the pure formatter WITH THE ORDINAL — not with `tries`, not with a literal; and
#     (b) this file must do NO arithmetic on `v.tries` at all. `v.tries` is still rendered raw by the two `NOT HEARD`
#         arms (there the number IS the measurement), so the string cannot simply be forbidden — the `+` is the defect.
# ⓘ `code_flat` strips comments first: the block above the arm QUOTES the old `v.tries + 1` line verbatim, so a bare
#    grep would read this file's own documentation as the violation. That is §B77, and both S4 checks failed that way.
w10() { code_flat "$1" | grep -qF 'mrui::emg_attempt_line(detail, sizeof detail, v.attempt_ordinal)' && \
        ! code_flat "$1" | grep -qE 'v\.tries[[:space:]]*\+'; }
wchk "W10 the FIRING arm renders the model's ordinal through the one formatter, never tries+1" \
     w10 's|mrui::emg_attempt_line(detail, sizeof detail, v.attempt_ordinal);|snprintf(detail, sizeof detail, "attempt %u of %u", unsigned(v.tries + 1), unsigned(mrui::kEmgMaxTries));|'
# W10b §B115, THE OTHER HALF OF THE LAST MILE — found by independent QA (2026-08-05) as a NO-GO on the §B115 slice, and
#     it is precisely the class W6 exists for. W10 guarded the CONSUMPTION (`emg_attempt_line(…, v.attempt_ordinal)`)
#     and NOTHING guarded the POPULATION in `freeze_outcome`. `OutcomeView::attempt_ordinal` has a default initializer
#     of `0` and `v` is `{}`-initialised, so DELETING the one assignment left the panel displaying `attempt 0 of 3`
#     while the whole native suite, W10, this entire probe and `heltec_v3` all stayed green — MEASURED, not reasoned:
#     both mutations below were applied to the live file and the probe reported 12/12 wiring over each of them.
# ★ TWO CLAUSES, and each has its own control below:
#     (a) the field is POPULATED FROM THE MODEL'S ACCESSOR — the ordinal is computed in the pure unit the native suite
#         drives, so any value composed here instead is a number no test can reach; and
#     (b) that is the ONLY write to the field in this file. Without (b) a later overwrite satisfies (a) and still lies —
#         the same "a success that isn't, one layer down" shape. Matched on `.attempt_ordinal =` so a write through ANY
#         object is seen (`v.`, `s_frame_out.`), while the struct's own `= 0` default initializer has no `.` and is not
#         counted; `[^=]` keeps `==` out.
w10b() { code_flat "$1" | grep -qE '\.attempt_ordinal = s_model\.emg_attempt_ordinal\(\)' || return 1
         [ "$(code_flat "$1" | grep -oE '\.attempt_ordinal[[:space:]]*=[^=]' | wc -l)" -eq 1 ]; }
wchk "W10b freeze_outcome populates attempt_ordinal from the model, and nothing else writes it" \
     w10b 's|v\.attempt_ordinal = s_model\.emg_attempt_ordinal();|v.attempt_ordinal = 0;|' \
          's|v\.attempt_ordinal = s_model\.emg_attempt_ordinal();|v.attempt_ordinal = v.tries;|' \
          's|v\.attempt_ordinal = s_model\.emg_attempt_ordinal();|v.attempt_ordinal = s_model.emg_attempt_ordinal(); v.attempt_ordinal = v.tries;|'
# W11 §B117 (OWNER-RULED 2026-08-05) — THE TERMINAL ALARM HEADLINE IS `NOT RELAYED`, and it needs a check for the same
#     reason the wording needed a ruling: `NOT HEARD` OVERSTATED the measurement (no relay was overheard) as a claim
#     about RECEPTION, and on the bench run the team had in fact received all three posts and replied. Every emergency
#     headline is a bare literal in a TU nothing compiles, so until W11 NOTHING in the tree could see this string
#     change back. `NOT RELAYED` states exactly what was measured and implies nothing about receipt.
# ⛔⛔ AND TWO STRINGS MUST STAY OUT, NOT ONE. The first ruled wording was `NO RELAY HEARD` (14 chars, clipped — see
#     W11b), and between the two rulings this file carried an 8-char `NO RELAY` that **NO OWNER EVER APPROVED**: a slice
#     substituted it and reported an approval it had invented. ⚠ THIS COMMENT USED TO REPEAT THAT INVENTED APPROVAL
#     ("which is why the owner approved the short form") — corrected here, audit trail kept (register B117). ⇒ the
#     absence clause names `NOT HEARD` **and** `NO RELAY` LITERALLY. ⛔ Do not weaken either to a substring or a prefix
#     match: `NO RELAY` is not a substring of `NOT RELAYED`, and a looser pattern would pass on any headline at all.
# ★ THREE CLAUSES: the ruled headline PRESENT, and each superseded string ABSENT from CODE. The absence half is what a
#   partial revert trips — and note the file's comments DISCUSS both old strings at length (they must, that is the
#   history), so the check reads `head = "…"` ASSIGNMENTS in comment-stripped code, never the bare words (§B77).
# ★★ FOUR CONTROLS, and the last two are the W10b lesson: two are REPLACEMENTS (which also break the presence clause,
#    so alone they would leave the absence clauses unmeasured) and two ADD a second assignment while leaving the ruled
#    one in place — the real hazard, because the later write WINS on the panel while a presence-only check stays green.
w11() { code_flat "$1" | grep -qF 'head = "NOT RELAYED"' || return 1
        ! code_flat "$1" | grep -qF 'head = "NOT HEARD"' || return 1
        ! code_flat "$1" | grep -qF 'head = "NO RELAY"'; }
wchk "W11 the terminal alarm headline is the ruled NOT RELAYED, never NOT HEARD or the unapproved NO RELAY" \
     w11 's|head = "NOT RELAYED";|head = "NOT HEARD";|' \
         's|head = "NOT RELAYED";|head = "NO RELAY";|' \
         's|head = "NOT RELAYED";|head = "NOT RELAYED"; head = "NOT HEARD";|' \
         's|head = "NOT RELAYED";|head = "NOT RELAYED"; head = "NO RELAY";|'
# W11b THE WIDTH GATE, and it is not decoration — it is the ONLY thing between a future padding/font change and a
#     TRUNCATED DISTRESS HEADLINE. `Font::large` is `u8g2_font_10x20_tf` = 10 px/char on a 128 px panel = 12 columns,
#     drawn at x = 0. `NOT RELAYED` is 11 chars = 110 px and fits with ONE COLUMN SPARE — and that spare column was a
#     deciding factor in the ruling: the rejected 12-char candidates (`NO REL HEARD`, `NO RELAY HRD`) spend the whole
#     budget. The first ruled wording `NO RELAY HEARD` is 14 chars = 140 px and u8g2 clips it to `NO RELAY HEAR`.
# ★ TWO CONTROLS, because "13 or more fails" is the property and a single 14-char control would not pin the BOUNDARY:
#   one at exactly 13 chars (the first value that must fail) and one at the 14-char first-ruled wording.
w11w() {   # every Font::large headline must fit 12 columns (10 px glyphs on a 128 px panel)
  local widest
  widest=$(code_of "$1" | grep -oE 'head = "[^"]*"' | sed 's/head = "//; s/"$//' | awk '{ print length($0) }' | sort -n | tail -1)
  [ -n "$widest" ] && [ "$widest" -le 12 ]; }
wchk "W11b no Font::large headline exceeds the 12-column large-font budget" \
     w11w 's|head = "NOT RELAYED";|head = "NOTHING HEARD";|' \
          's|head = "NOT RELAYED";|head = "NO RELAY HEARD";|'
# W12 ★★★ §UI-14 follow-up / [[B194]] — THE IMMEDIATE CONFLICT NOTIFICATION, AND IT IS THE FIRST WIRING CHECK IN THIS
#     FILE THAT READS A DIFFERENT SOURCE. Spec §3.6.1 requires a serial/BLE write during an open OLED draft to produce
#     `CFG! RELOAD` IMMEDIATELY; the panel half is measured by `tools/probe_firmware_ui/`, but the CALL — in
#     `handle_cfg_set`, a function no host build compiles — is reachable by nothing else.
# ★★★ ONE PREDICATE, FOUR WRONG ANSWERS, and the reason it is one literal sequence rather than a bare presence grep:
#     the property is not "the call exists", it is "the call happens AFTER a write that BOTH happened and SUCCEEDED".
#     Matching the failure branch's exact body is what makes an added call INSIDE it fail the check — the wrong answer
#     a presence-only grep would have passed (§B115's W10b lesson: a check whose only control is a deletion measures
#     half the property).
#       (a) the call deleted            -> no notification at all, i.e. the blocker back;
#       (b) the call moved BEFORE the save -> notifies on a write that may then FAIL;
#       (c) the `persist` guard dropped -> notifies on a LIVE-ONLY key with no durable record to disagree with;
#       (d) a call ADDED to the failure branch -> claims a change on a write that did not happen.
CFG_CPP="$ROOT/src/firmware_config.cpp"
# ⚠ TWO CLAUSES, and the second was ADDED after control (b) proved the first insufficient — MEASURED, not foreseen: a
#   revert that inserted a SECOND call before the write left the good sequence intact and the check passed. ⇒ the
#   property is "there are EXACTLY SEVEN notification sites, and THIS one is after a successful persisted write". The
#   count clause is what makes (b) and (d) — both of which ADD a call rather than move one — actually fail.
# ★★ GENERALISED 2026-08-13 ([[B194]] / §notify-every-save): the rule is now SEVEN user-initiated `/mrcfg` writers, so
#    the predicate is SHARED rather than copy-pasted seven times (U1 — a second copy is how two harnesses drift). Both
#    clauses stay load-bearing, and they divide the wrong answers between them:
#      · the COUNT clause catches every control that ADDS a call (before the write, or inside a failure branch);
#      · the SEQUENCE clause catches a deletion, a dropped guard, and a dropped `return` that lets the notification
#        run after a FAILED write.
#    ⛔⛔ CORRECTED IN PLACE 2026-08-13 (QG round 2) — THIS BLOCK USED TO CLAIM A PROPERTY THE CODE DID NOT HAVE, and
#    that is the worse half of the defect, because the comment is the instruction a reader trusts. It read: *"The
#    count is a SINGLE constant on purpose: a new writer that forgets to notify does not merely leave its own check
#    unwritten — it makes all seven fail loudly, which is the maintenance property §notify-every-save exists for."*
#    **THAT WAS FALSE AS WRITTEN.** The count looked at `mr_ui_on_config_saved()` ONLY, so an EIGHTH `mrnv::save(`
#    added with NO notification left the count at seven and every check GREEN — precisely the omission the sentence
#    claimed to catch, i.e. an instrument that cannot fail hiding behind a comment that says it can.
#    ✅ THE FIX IS THE OTHER HALF OF THE COUNT: `cfg_writer_counts_ok` counts the file's `/mrcfg` WRITES too and
#    requires them to BALANCE the notifications. The withdrawn sentence is now true of the code below — but only
#    within the scope stated next, which is why the scope is stated rather than left to be inferred.
# ⛔⛔ SCOPE LIMIT, STATED PLAINLY SO THE CLAIM IS NOT WIDER THAN THE GUARD: this is a PER-FILE check on
#    `src/firmware_config.cpp`. A new user-initiated `/mrcfg` verb added in a DIFFERENT file is NOT caught by it.
#    ⛔ No whole-tree scan is built here; the residue is named, not silently absorbed.
# ★★ THREE CONSTANTS, and the third exists because the arithmetic must not be able to go silently wrong: the file's
#    `mrnv::save(` occurrences are the SEVEN verbs PLUS `DeviceCfgStore::save` (the `ICfgStore` override — the
#    service's own store, not a verb), so the override is subtracted BY MATCHING ITS LINE, and the match itself is
#    asserted. ⓘ VERIFIED rather than assumed (V1): the file holds 8 `mrnv::save(` occurrences, and `grep -oF` does
#    NOT match `mrnv::save_id(` (lines 231/242, the `/mrid` writer) — the paren excludes it, not luck.
CFG_NOTIFY_SITES=7                 # the seven USER-INITIATED verbs — bump this ONLY together with a new W-check
CFG_STORE_SAVE='bool save(const mrnv::Blob& b) override { return mrnv::save(b); }'   # the ONE exempt save
cfg_writer_counts_ok() {  # the SHARED tripwire: every non-exempt /mrcfg write in this file has a notification
  local notifies saves exempt
  notifies=$(code_flat "$1" | grep -oF 'mr_ui_on_config_saved()' | grep -c .)
  saves=$(code_flat "$1"   | grep -oF 'mrnv::save('            | grep -c .)
  exempt=$(code_flat "$1" | tr -s ' ' | grep -oF "$CFG_STORE_SAVE" | grep -c .)
  [ "$exempt" -eq 1 ]                        || return 1   # the override must stay identifiable, or the sum lies
  [ "$notifies" -eq "$CFG_NOTIFY_SITES" ]    || return 1   # the PIN: any new writer forces the harness to be updated
  [ "$((saves - exempt))" -eq "$notifies" ]; }             # the TRIPWIRE: a BARE save is an unnotified writer
nsite() {  # nsite(file, literal-comment-stripped-space-squeezed-sequence)
  cfg_writer_counts_ok "$1" || return 1
  code_flat "$1" | tr -s ' ' | grep -qF "$2"; }
w12() { nsite "$1" 'if (persist && !mrnv::save(b)) { out.println(F("> cfg err nv_save_failed")); return; } if (persist) mr_ui_on_config_saved();'; }
wchk_in "$CFG_CPP" "W12 handle_cfg_set NOTIFIES the panel after a successful persisted /mrcfg write" \
     w12 's|    if (persist) mr_ui_on_config_saved();||' \
         's|    if (persist && !mrnv::save(b)) { out.println(F("> cfg err nv_save_failed")); return; }|    if (persist) mr_ui_on_config_saved();\n    if (persist \&\& !mrnv::save(b)) { out.println(F("> cfg err nv_save_failed")); return; }|' \
         's|    if (persist) mr_ui_on_config_saved();|    mr_ui_on_config_saved();|' \
         's|{ out.println(F("> cfg err nv_save_failed")); return; }|{ mr_ui_on_config_saved(); out.println(F("> cfg err nv_save_failed")); return; }|'
# W13 ⛔ AND THE HOOK MUST STAY FEATURE-NEUTRAL: `src/firmware_config.cpp` may not learn that a panel exists. The
#     no-op arm lives in `lib/hal/mr_ui.h`, exactly as it does for the other three hooks, so an `MR_FEAT_OLED` guard
#     appearing around this call would mean the config path had grown a display dependency.
#     ★ Its control ADDS the guard rather than removing anything, because "no `#if` here" cannot be reverted by deletion.
w13() { ! code_flat "$1" | grep -qE 'MR_FEAT_OLED'; }
wchk_in "$CFG_CPP" "W13 the config path stays feature-neutral (no MR_FEAT_OLED around the hook)" \
     w13 's|    if (persist) mr_ui_on_config_saved();|#if MR_FEAT_OLED\n    if (persist) mr_ui_on_config_saved();\n#endif|'
# ================================================================================================ W14-W19
# ★★★ §notify-every-save / [[B194]] — THE OTHER SIX USER-INITIATED `/mrcfg` WRITERS. `handle_cfg_set` (W12) was only
#     the first: `leave` RESETS ALL FOUR covered fields (`b = mrnv::Blob{}`) and persisted them while telling the panel
#     nothing, which is the blocker this slice was dispatched on. All six live in `src/firmware_config.cpp`, which no
#     host build compiles — the same reason W12 exists here rather than in a native case.
# ★★ EACH CHECK CARRIES FOUR CONTROLS (W19 five), and they are the same four wrong answers every time, which is the
#    point of a RULE: (a) the call deleted -> the blocker back; (b) a call added BEFORE the write -> notifies on a
#    write that may then FAIL; (c) the failure branch's `return`/guard dropped -> notifies after a FAILED write;
#    (d) a call added INSIDE the failure branch -> claims a change on a write that did not happen.
w14() { nsite "$1" 'if (!mrnv::save(b)) { out.println(F("> gateway err nv_save_failed")); return; } mr_ui_on_config_saved();'; }
wchk_in "$CFG_CPP" "W14 handle_gateway NOTIFIES after its successful /mrcfg write" \
     w14 '/gateway err nv_save_failed/{n;s|mr_ui_on_config_saved();|;|;}' \
         's|    if (!mrnv::save(b)) { out.println(F("> gateway err nv_save_failed")); return; }|    mr_ui_on_config_saved();\n    if (!mrnv::save(b)) { out.println(F("> gateway err nv_save_failed")); return; }|' \
         's|(F("> gateway err nv_save_failed")); return; }|(F("> gateway err nv_save_failed")); }|' \
         's|{ out.println(F("> gateway err nv_save_failed")); return; }|{ mr_ui_on_config_saved(); out.println(F("> gateway err nv_save_failed")); return; }|'
w15() { nsite "$1" 'if (!mrnv::save(b)) { out.println(F("> join err nv_save_failed")); return; } mr_ui_on_config_saved();'; }
wchk_in "$CFG_CPP" "W15 handle_join NOTIFIES after its successful /mrcfg write" \
     w15 '/join err nv_save_failed/{n;s|mr_ui_on_config_saved();|;|;}' \
         's|        if (!mrnv::save(b)) { out.println(F("> join err nv_save_failed")); return; }|        mr_ui_on_config_saved();\n        if (!mrnv::save(b)) { out.println(F("> join err nv_save_failed")); return; }|' \
         's|(F("> join err nv_save_failed")); return; }|(F("> join err nv_save_failed")); }|' \
         's|{ out.println(F("> join err nv_save_failed")); return; }|{ mr_ui_on_config_saved(); out.println(F("> join err nv_save_failed")); return; }|'
w16() { nsite "$1" 'if (!mrnv::save(b)) { out.println(F("> create err nv_save_failed")); return; } mr_ui_on_config_saved();'; }
wchk_in "$CFG_CPP" "W16 handle_create NOTIFIES after its successful /mrcfg write" \
     w16 '/create err nv_save_failed/{n;s|mr_ui_on_config_saved();|;|;}' \
         's|        if (!mrnv::save(b)) { out.println(F("> create err nv_save_failed")); return; }|        mr_ui_on_config_saved();\n        if (!mrnv::save(b)) { out.println(F("> create err nv_save_failed")); return; }|' \
         's|(F("> create err nv_save_failed")); return; }|(F("> create err nv_save_failed")); }|' \
         's|{ out.println(F("> create err nv_save_failed")); return; }|{ mr_ui_on_config_saved(); out.println(F("> create err nv_save_failed")); return; }|'
# ⚠⚠ W17 IS THE ONE WITH A THIRD STATE. `handle_team`'s save is §3-A.4's UNCHECKED one: not success /
#    failure-with-return but success · FAILURE-BUT-LIVE-ANYWAY (the live team state is deliberately not rolled back).
#    ⇒ the verdict is CAPTURED into `team_saved` so the notification rides the success arm only; the check pins that
#    capture, and control (c) is the wrong answer that ignores it and notifies either way.
w17() { nsite "$1" 'const bool team_saved = mrnv::save(b); if (team_saved) mr_ui_on_config_saved(); else out.println(F("> team err nv_save_failed'; }
wchk_in "$CFG_CPP" "W17 handle_team CAPTURES its save verdict and NOTIFIES only on the success arm" \
     w17 's|    if (team_saved) mr_ui_on_config_saved(); else out.println|    if (!team_saved) out.println|' \
         's|    const bool team_saved = mrnv::save(b);|    mr_ui_on_config_saved();\n    const bool team_saved = mrnv::save(b);|' \
         's|    if (team_saved) mr_ui_on_config_saved(); else out.println|    mr_ui_on_config_saved(); if (!team_saved) out.println|' \
         's|    if (team_saved) mr_ui_on_config_saved(); else out.println|    if (team_saved) mr_ui_on_config_saved(); else mr_ui_on_config_saved(), out.println|'
w18() { nsite "$1" 'if (!mrnv::save(b)) { out.println(F("> leave err nv_save_failed")); return; } mr_ui_on_config_saved();'; }
wchk_in "$CFG_CPP" "W18 handle_leave NOTIFIES after its successful /mrcfg write (THE BLOCKER: it resets all four covered fields)" \
     w18 '/leave err nv_save_failed/{n;s|mr_ui_on_config_saved();|;|;}' \
         's|    if (!mrnv::save(b)) { out.println(F("> leave err nv_save_failed")); return; }|    mr_ui_on_config_saved();\n    if (!mrnv::save(b)) { out.println(F("> leave err nv_save_failed")); return; }|' \
         's|(F("> leave err nv_save_failed")); return; }|(F("> leave err nv_save_failed")); }|' \
         's|{ out.println(F("> leave err nv_save_failed")); return; }|{ mr_ui_on_config_saved(); out.println(F("> leave err nv_save_failed")); return; }|'
# ⚠ W19 CARRIES A FIFTH CONTROL, and it guards something that is NOT this slice's property: the `memset` that wipes
#   the derived admin keypair sits BETWEEN the save and the guard, and it must run on BOTH arms. The sequence clause
#   spans it, so a revert that removes or relocates the wipe turns this check red — deliberately, because "notify on
#   the success side" was the ONLY thing this slice was allowed to change at this site.
w19() { nsite "$1" 'const bool saved = mrnv::save(b); memset(&admin, 0, sizeof admin); if (!saved) { out.println(F("> password err: nv_save_failed")); return; } mr_ui_on_config_saved();'; }
wchk_in "$CFG_CPP" "W19 handle_password NOTIFIES on the success side of its existing verdict, wipe untouched" \
     w19 '/password err: nv_save_failed/{n;s|mr_ui_on_config_saved();|;|;}' \
         's|    const bool saved = mrnv::save(b);|    mr_ui_on_config_saved();\n    const bool saved = mrnv::save(b);|' \
         's|    if (!saved) { out.println(F("> password err: nv_save_failed")); return; }||' \
         's|{ out.println(F("> password err: nv_save_failed")); return; }|{ mr_ui_on_config_saved(); out.println(F("> password err: nv_save_failed")); return; }|' \
         's|    memset(&admin, 0, sizeof admin);|    ;|'
# ================================================================================================ W20
# ⛔⛔ W20 IS THE FUTURE-WRITER TRIPWIRE ITSELF, AND IT EXISTS BECAUSE THE FIRST VERSION OF IT WAS VACUOUS (QG round 2).
#     W12-W19 each pin ONE site's placement; none of them — and, before this check, no clause anywhere — could see a
#     NEW `/mrcfg` writer that never notified at all. The count was over `mr_ui_on_config_saved()` alone, so an eighth
#     `mrnv::save(` with no notification left it at seven and the whole file stayed GREEN.
# ★★★ CONTROL (a) IS THE WHOLE POINT OF THIS CHECK: it inserts an EIGHTH BARE SAVE — a new verb that forgot — and the
#     gate must turn RED. A two-clause count whose "somebody added a save" arm was never run is the same defect with a
#     larger constant, which is exactly what the previous round shipped.
# ★ (b) proves the PIN is live: a new verb that DOES notify also turns it red, because the harness must be updated
#   alongside it (bump `CFG_NOTIFY_SITES` and add its own W-check). That is the intended maintenance burden, not a bug.
# ★ (c) proves the SUBTRACTION cannot go silently wrong: make `DeviceCfgStore::save` unrecognisable and the exemption
#   count drops to 0, which must fail LOUDLY rather than quietly mis-total.
# ★ (d) is the converse arm of the balance: a save left in place with its notification removed.
# ⚠ ITS ONE BLIND SPOT, STATED RATHER THAN IMPLIED: moving a verb's notification INTO the exempt store override would
#   keep both totals balanced and W20 would pass. **W12-W19's per-site SEQUENCE clauses are what catch that** — W20
#   guards "no unnotified writer", they guard "each notification is at its own site, after its own successful save".
#   ⛔ And the scope limit above still applies: a new verb in a DIFFERENT file is caught by neither.
w20() { cfg_writer_counts_ok "$1"; }
wchk_in "$CFG_CPP" "W20 every non-exempt /mrcfg write in the file has a notification (the future-writer tripwire)" \
     w20 's|    out.print(F("> left network (kept freq="));|    { mrnv::Blob nb{}; (void)mrnv::save(nb); }\n    out.print(F("> left network (kept freq="));|' \
         's|    out.print(F("> left network (kept freq="));|    { mrnv::Blob nb{}; (void)mrnv::save(nb); mr_ui_on_config_saved(); }\n    out.print(F("> left network (kept freq="));|' \
         's|bool save(const mrnv::Blob& b) override { return mrnv::save(b); }|bool save(const mrnv::Blob\& b) override { const bool ok = mrnv::save(b); return ok; }|' \
         '/leave err nv_save_failed/{n;s|mr_ui_on_config_saved();|;|;}'
# ================================================================================================ W21
# ★★★ §T2 / [[B189]] — THE SOLE PRODUCTION BRIDGE FROM THE DEVICE-HAL OUTCOME RING INTO CORE. Native tests can
#     drive `DeviceHal` and `Node::on_tx_complete` independently, but `src/fw_main.cpp` is outside that build; the
#     simulator has no device outcome ring. Deleting this loop therefore leaves both halves green while every metal
#     completion is silently stranded. The exact sequence pins all four caller obligations together:
#       (a) `service_tx()` runs first, so a completion is produced before the drain;
#       (b) `pop_tx_outcome(outcome)` is the `for` condition, so the bounded ring is drained exhaustively;
#       (c) the popped object itself reaches `g_node.on_tx_complete(outcome)` on every iteration; and
#       (d) no hand-replicated dispatch or different destination method stands in for the core entry point.
# ★ FOUR CONTROLS, one per wrong answer named by QG: delete the drain; reverse producer/drain order; replace the loop
#   with a single `if` pop; or discard each outcome into the wrong Node method. `wchk_in` additionally proves every
#   mutation changes the source copy before requiring the predicate to turn RED.
FW_MAIN="$ROOT/src/fw_main.cpp"
w21() {
  code_flat "$1" | grep -qE 'g_hal\.service_tx\(\);[[:space:]]*for \(meshroute::TxOutcome outcome; g_hal\.pop_tx_outcome\(outcome\); \)[[:space:]]*g_node\.on_tx_complete\(outcome\);'
}
wchk_in "$FW_MAIN" "W21 service_tx precedes an exhaustive outcome drain into Node::on_tx_complete" \
     w21 '/g_hal\.pop_tx_outcome(outcome)/d' \
         's|    g_hal.service_tx();|__W21_SERVICE__|; s|    for (meshroute::TxOutcome outcome; g_hal.pop_tx_outcome(outcome); ) g_node.on_tx_complete(outcome);|    g_hal.service_tx();|; s|__W21_SERVICE__|    for (meshroute::TxOutcome outcome; g_hal.pop_tx_outcome(outcome); ) g_node.on_tx_complete(outcome);|' \
         's|    for (meshroute::TxOutcome outcome; g_hal.pop_tx_outcome(outcome); ) g_node.on_tx_complete(outcome);|    meshroute::TxOutcome outcome; if (g_hal.pop_tx_outcome(outcome)) g_node.on_tx_complete(outcome);|' \
         's|g_node.on_tx_complete(outcome)|g_node.on_radio_busy({})|'
echo "structural: $s_pass passed / $s_fail failed / $((s_pass+s_fail)) total"
echo "wiring:     $w_pass passed / $w_fail failed / $((w_pass+w_fail)) total; $w_ctl negative control(s) verified RED"
[ "$s_fail" -eq 0 ] || rc=1
[ "$w_fail" -eq 0 ] || rc=1

if [ "${1:-}" != "--no-neg" ]; then
  echo
  echo "== negative controls (each MUST fail) =="
  [ -f "$HERE/negctl.py" ] || { echo "negctl.py missing"; exit 1; }
  # ★ Pass the paths AND the compiler config, so the controls cannot drift from the probe they are controlling.
  python3 "$HERE/negctl.py" "$BOARD/board_ui.cpp" "$OUT" "$CXX" \
     -std=gnu++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror \
     "${BOARD_DEFS[@]}" \
     -I"$HERE/fakes" -I"$BOARD" -I"$ROOT/lib/hal" -I"$ROOT/lib/core" -I"$ROOT/src" || rc=1
fi
exit $rc
