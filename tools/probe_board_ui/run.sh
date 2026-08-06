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

# ⚠ These two -D MUST mirror `[env:heltec_v3]` (`platformio.ini:221`/`:227`). If they drift, the probe measures a
#   configuration the board never builds — which is the same vacuous-instrument failure the controls exist to catch.
build() {   # build($1 = board_ui.cpp path, $2 = output binary)
  "$CXX" -std=gnu++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror \
     -DMR_FEAT_OLED=1 -DMR_UI_BTN_PIN=0 \
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
   -DMR_FEAT_OLED=1 -DMR_UI_BTN_PIN=0 \
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
wchk() {  # wchk(label, predicate-fn, sed-script-that-reverts-the-property [, further sed scripts ...])
  local label=$1 pred=$2; shift 2
  if ! "$pred" "$FW_UI"; then w_fail=$((w_fail+1)); echo "  FAIL $label (not wired in firmware_ui.cpp)"; return; fi
  local n=0 revert
  for revert in "$@"; do
    n=$((n+1))
    sed "$revert" "$FW_UI" > "$OUT/fw_ui_revert.cpp"
    if cmp -s "$FW_UI" "$OUT/fw_ui_revert.cpp"; then
      w_fail=$((w_fail+1)); echo "  FAIL $label CONTROL $n — the revert changed NOTHING, so the check is vacuous"; return
    fi
    if "$pred" "$OUT/fw_ui_revert.cpp"; then
      w_fail=$((w_fail+1)); echo "  FAIL $label CONTROL $n — the check still passes against the reverted copy"; return
    fi
    w_ctl=$((w_ctl+1))
  done
  w_pass=$((w_pass+1))
}
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
     -DMR_FEAT_OLED=1 -DMR_UI_BTN_PIN=0 \
     -I"$HERE/fakes" -I"$BOARD" -I"$ROOT/lib/hal" -I"$ROOT/lib/core" -I"$ROOT/src" || rc=1
fi
exit $rc
