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
# S2b §B197's FIFTH hook. Separate from the loop above because it is the only one that RETURNS something — and the
#     return is the whole seam: `bool`, not `void`. A `void` here would not link against `lib/hal/mr_ui.h`'s
#     declaration at all, which is a stronger backstop than this grep; the grep is what says WHICH TU owns it.
schk "S2b src/firmware_ui.cpp defines mr_ui_allows_sleep" "grep -qE '^bool mr_ui_allows_sleep\\(' '$FW_UI'"
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
# ⛔⛔ RE-PINNED 2026-08-16 (§CHROME-4). Two of this predicate's four clauses named things the 19-column body
#    migration moved, and BOTH would have gone silently false rather than loudly wrong:
#      · the draw call is now `body_text(...)`, the ONE `kBodyX = 12` authority design §7.1 rule 1 requires, not a
#        bare `mrui::draw_text(0, …)`;
#      · the string is `TEAMMATE GONE, pick` — 19 columns exactly. The old `TEAMMATE GONE, repick` was 21 and was
#        SIZED TO THE OLD BODY (its own comment said so); at 19 the panel would have clipped it to `…, repi`, which
#        §7.1 rule 5 forbids as a truncation policy. ★ Both halves of §B64's ruling are intact — the row still names
#        the fact AND gives the remedy, and the `>` highlight is still suppressed.
w9() { code_flat "$1" | grep -qE 'st\.team_pick_gone \? uint8_t\(kBodyRows - 1\)' && \
       code_flat "$1" | grep -qE '!st\.team_pick_gone && first \+ row == st\.cursor' && \
       code_flat "$1" | grep -qF 'st.team_pick_gone) body_text' && \
       code_flat "$1" | grep -qF 'TEAMMATE GONE, pick'; }
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
# ★★★ UPDATED 2026-08-17 BY §PROV-TX ([[B207]]) — AND THE ARITHMETIC GAINED A THIRD TERM, DELIBERATELY RATHER THAN BY
#     RELAXING THE PIN. `handle_team` no longer calls `mrnv::save` at all: its `/mrcfg` write now goes through the
#     provisioning TRANSACTION, i.e. through `ICfgStore::save` — the SAME override this check exempts as "the service's
#     own store, not a verb". So the file holds SEVEN direct `mrnv::save(` (six verbs + that override) while still
#     holding SEVEN notifications, and the old `saves - exempt == notifies` balance would report 6 == 7 and FAIL.
#     MEASURED, not assumed: at HEAD the file had 8 saves / 7 notifies; after the slice it has 7 / 7, and every one of
#     W12-W20 went red on the shared predicate until this term was added.
# ★★ THE CREDIT IS EARNED, NOT ASSERTED: `CFG_ROUTED_SITES` is only granted if the ROUTE ITSELF is present, matched by
#    the transaction call. A future refactor that deletes the route but keeps the credit drops `routed` to 0 and fails
#    LOUDLY — the same discipline the `exempt` term already has, and the reason the term is a counted match rather than
#    a constant subtracted from the total.
# ⛔⛔ AND THE BLIND SPOT THIS WIDENS IS STATED RATHER THAN ABSORBED: a NEW verb that also writes through
#    `device_cfg_store()` adds NO direct `mrnv::save(`, so the balance no longer sees it — it would have to bump
#    `CFG_ROUTED_SITES` and add its own W-check, exactly as a direct writer must bump `CFG_NOTIFY_SITES`. W20's
#    control (e) is what keeps that honest for the ONE route that exists today.
CFG_NOTIFY_SITES=7                 # the seven USER-INITIATED verbs — bump this ONLY together with a new W-check
CFG_STORE_SAVE='bool save(const mrnv::Blob& b) override { return mrnv::save(b); }'   # the ONE exempt save
CFG_ROUTED_SITES=1                 # §PROV-TX: verbs whose /mrcfg write goes through ICfgStore::save, not mrnv::save
CFG_ROUTED_CALL='prov_service().apply_team('                                        # …and the route that earns it
cfg_writer_counts_ok() {  # the SHARED tripwire: every non-exempt /mrcfg write in this file has a notification
  local notifies saves exempt routed
  notifies=$(code_flat "$1" | grep -oF 'mr_ui_on_config_saved()' | grep -c .)
  saves=$(code_flat "$1"   | grep -oF 'mrnv::save('            | grep -c .)
  exempt=$(code_flat "$1" | tr -s ' ' | grep -oF "$CFG_STORE_SAVE" | grep -c .)
  routed=$(code_flat "$1" | tr -s ' ' | grep -oF "$CFG_ROUTED_CALL" | grep -c .)
  [ "$exempt" -eq 1 ]                        || return 1   # the override must stay identifiable, or the sum lies
  [ "$routed" -eq "$CFG_ROUTED_SITES" ]      || return 1   # ★ the routed credit must be EARNED by a present route
  [ "$notifies" -eq "$CFG_NOTIFY_SITES" ]    || return 1   # the PIN: any new writer forces the harness to be updated
  [ "$((saves - exempt + routed))" -eq "$notifies" ]; }    # the TRIPWIRE: a BARE save is an unnotified writer
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
# ⚠⚠ W17 REWRITTEN 2026-08-17 BY §PROV-TX ([[B207]]). ⛔ THE OLD PROPERTY IS GONE BECAUSE THE DEFECT IT DESCRIBED IS
#    GONE, and the withdrawn wording is kept here because it is the history a reader needs: it pinned
#    *"const bool team_saved = mrnv::save(b); if (team_saved) mr_ui_on_config_saved(); else out.println(…)"* — §3-A.4's
#    THIRD state, success · FAILURE-BUT-LIVE-ANYWAY, in which the live team state was deliberately NOT rolled back. That
#    third state WAS [[B207]]: the verb mutated (and TRANSMITTED) before persisting, so a failed write left the node in a
#    team it would silently leave at the next reboot.
# ★★ THE NEW PROPERTY IS STRICTLY STRONGER AND STRICTLY SIMPLER, because the transaction is atomic: there is now ONE
#    verdict to notify on. `handle_team` guards on the TYPED verdict and notifies ONLY on `applied` — so `no_change`
#    (a real outcome that performs ZERO writes) must NOT notify either, which the old two-arm shape could not even
#    express. The one-line guard is what makes it a compact source fact; the strings live in `team_report_not_applied`.
# ★ FOUR CONTROLS — the same wrong answers as W14-W16, re-aimed at this shape:
#     (a) the guard+notification pair deleted;   (b) a notification BEFORE the transaction (the count clause catches it);
#     (c) the guard's `return` dropped, so a refusal or a no_change falls THROUGH into the notification;
#     (d) a notification added inside the not-applied branch — the `no_change` mis-notify, i.e. [[B194]] inverted.
w17() { nsite "$1" 'if (res.verdict != mrfw::ProvVerdict::applied) { team_report_not_applied(res, out); return; } mr_ui_on_config_saved();'; }
wchk_in "$CFG_CPP" "W17 handle_team NOTIFIES only on the transaction's applied verdict" \
     w17 's|    if (res.verdict != mrfw::ProvVerdict::applied) { team_report_not_applied(res, out); return; }||' \
         's|    const mrfw::ProvResult res = prov_service().apply_team(rq, c, snap);|    mr_ui_on_config_saved();\n    const mrfw::ProvResult res = prov_service().apply_team(rq, c, snap);|' \
         's|{ team_report_not_applied(res, out); return; }|{ team_report_not_applied(res, out); }|' \
         's|{ team_report_not_applied(res, out); return; }|{ team_report_not_applied(res, out); mr_ui_on_config_saved(); return; }|'
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
         '/leave err nv_save_failed/{n;s|mr_ui_on_config_saved();|;|;}' \
         's|prov_service().apply_team(rq, c, snap)|prov_service().apply_team_renamed(rq, c, snap)|'
# ================================================================================================ W21
# ★★★ §T2 / [[B189]] — THE SOLE PRODUCTION BRIDGE FROM THE DEVICE-HAL OUTCOME RING INTO CORE. Native tests can
#     drive `DeviceHal` and `Node::on_tx_complete` independently, but `src/fw_main.cpp` is outside that build; the
#     simulator has no device outcome ring. Deleting this loop therefore leaves both halves green while every metal
#     completion is silently stranded. The exact sequence pins all four caller obligations together:
#       (a) `collect_tx_completion()` runs first, so a completion is produced before the drain;
#       (b) `pop_tx_outcome(outcome)` is the `for` condition, so the bounded ring is drained exhaustively;
#       (c) the popped object itself reaches `g_node.on_tx_complete(outcome)` on every iteration; and
#       (d) no hand-replicated dispatch or different destination method stands in for the core entry point.
# ⛔ UPDATED FOR §T3 §2.1: the producer half is now `collect_tx_completion()`, not the old combined `service_tx()`.
#    W22 below is what pins WHERE that pair sits relative to the timer drain — this check pins only the pair itself.
# ★ FOUR CONTROLS, one per wrong answer named by QG: delete the drain; reverse producer/drain order; replace the loop
#   with a single `if` pop; or discard each outcome into the wrong Node method. `wchk_in` additionally proves every
#   mutation changes the source copy before requiring the predicate to turn RED.
FW_MAIN="$ROOT/src/fw_main.cpp"
w21() {
  code_flat "$1" | grep -qE 'g_hal\.collect_tx_completion\(\);[[:space:]]*for \(meshroute::TxOutcome outcome; g_hal\.pop_tx_outcome\(outcome\); \)[[:space:]]*g_node\.on_tx_complete\(outcome\);'
}
wchk_in "$FW_MAIN" "W21 collect_tx_completion precedes an exhaustive drain into on_tx_complete" \
     w21 '/g_hal\.pop_tx_outcome(outcome)/d' \
         's|    g_hal.collect_tx_completion();|__W21_COLLECT__|; s|    for (meshroute::TxOutcome outcome; g_hal.pop_tx_outcome(outcome); ) g_node.on_tx_complete(outcome);|    g_hal.collect_tx_completion();|; s|__W21_COLLECT__|    for (meshroute::TxOutcome outcome; g_hal.pop_tx_outcome(outcome); ) g_node.on_tx_complete(outcome);|' \
         's|    for (meshroute::TxOutcome outcome; g_hal.pop_tx_outcome(outcome); ) g_node.on_tx_complete(outcome);|    meshroute::TxOutcome outcome; if (g_hal.pop_tx_outcome(outcome)) g_node.on_tx_complete(outcome);|' \
         's|g_node.on_tx_complete(outcome)|g_node.on_radio_busy({})|'
# ================================================================================================ W22
# ★★★ §T3 §2.1 — THE LOOP **ORDER**, AND IT IS A DIFFERENT PROPERTY FROM W21. W21 would stay green with the whole
#     collect+drain pair sitting AFTER the timer loop, which is where it used to be and which LOSES the channel
#     post's completion outright: `kMBcastClearTimerId` does `_pending_tx.reset(); become_free();` 5 ms after the
#     calculated M-frame airtime, so on a loop pass delayed past both deadlines the timer deletes the flight before
#     the TxDone edge is ever collected and `send_aired` can never be attributed. The order is:
#       collect_tx_completion() -> drain -> the Node timer loop -> pump_tx().
# ★ THREE CONTROLS, each a plausible wrong answer rather than a deletion: (a) move the collect+drain BACK below the
#   timer loop (the pre-§T3 order); (b) move `pump_tx()` UP in front of the timer loop (which would starve a frame a
#   timer enqueues of its pass); (c) re-merge the two halves by pumping straight after the collect.
# ⚠ The predicate reads the FLATTENED source, so it is an order assertion over the real statements, not over comments.
w22() {
  code_flat "$1" | grep -qE 'g_hal\.collect_tx_completion\(\);.*g_node\.on_tx_complete\(outcome\);.*for \(int id; \(id = g_hal\.pop_due_timer\(\)\) >= 0; \).*g_hal\.pump_tx\(\);'
}
wchk_in "$FW_MAIN" "W22 collect+drain run BEFORE the timer loop and pump_tx AFTER it (§T3 §2.1)" \
     w22 's|^    g_hal.collect_tx_completion();$|__W22_MOVED__|; s|^    for (int id; (id = g_hal.pop_due_timer()).*$|\&\n    g_hal.collect_tx_completion();|; s|^__W22_MOVED__$|    ;|' \
         's|^    g_hal.pump_tx();$|    ;|; s|^    for (int id; (id = g_hal.pop_due_timer()).*$|    g_hal.pump_tx();\n\&|' \
         's|^    g_hal.pump_tx();$|    ;|; s|^    g_hal.collect_tx_completion();$|    g_hal.collect_tx_completion();\n    g_hal.pump_tx();|'
# ================================================================================================ W23-W32
# ⛔⛔ §B200 RETARGETED W26/W27/W28 AND ADDED W29-W32, AND THE REASON IS THE POINT OF THIS WHOLE BLOCK: the §B197
#     versions PINNED "the wake is armed ONCE, at boot, by mr_ui_init()". That is [[B200]] — a level-triggered GPIO
#     interrupt nothing ever disarms, which storms the shared ISR whenever the button is held and trips the Interrupt
#     watchdog. ⇒ THOSE CHECKS WOULD HAVE GONE GREEN AGAINST THE DEFECT. They are retargeted, not supplemented.
# ★★★ §B197/§B198 — THE SLEEP SEAM. Five files, none of which any behavioural gate can reach: `src/fw_main.cpp` is
#     outside the native build and outside the simulator; `lib/hal/mr_ui.h`'s non-OLED arm is compiled only by board
#     envs; `src/firmware_ui.cpp`'s fail-closed latch is reachable by `tools/probe_firmware_ui` but its CALL SITE in
#     the loop is not. The PURE halves (`InputFsm::active`, `mrui::ui_allows_sleep`) are under the native gate.
# ⚠ `flat1` squeezes runs of spaces as well as stripping comments, because the sleep gate is now a TWO-LINE `if` and a
#   continuation indent is not a property anybody should be able to break this check with.
flat1() { code_flat "$1" | tr -s ' '; }
# W23 THE GATE ITSELF, AND ITS ORDER. Three properties in one sequence, which is what makes each control a plausible
#     wrong answer rather than a deletion:
#       (a) `mr_ui_tick()` runs BEFORE the gate — the UI must have serviced this pass (advanced its gesture, pushed a
#           page, possibly blanked) before its answer is consulted, or the gate reads last pass's state;
#       (b) `mr_ui_allows_sleep()` is IN the gate, ANDed;
#       (c) the four EXISTING terms are all still there — a fix that quietly dropped `!serial_has_input()` would let a
#           node sleep on a host that had just typed.
w23() { flat1 "$1" | grep -qE 'mr_ui_tick\(\(uint32_t\)now\);.*if \(may_sleep && mr_ui_allows_sleep\(\) && !g_iradio\.tx_busy\(\) && g_hal\.txq_depth\(\) == 0 && !serial_has_input\(\) && !mrble::connected\(\)\)'; }
wchk_in "$FW_MAIN" "W23 the sleep gate consults mr_ui_allows_sleep() after mr_ui_tick, terms intact" \
     w23 's|    if (may_sleep \&\& mr_ui_allows_sleep() \&\&|    if (may_sleep \&\&|' \
         's|    if (may_sleep \&\& mr_ui_allows_sleep() \&\&|    if (may_sleep \|\| mr_ui_allows_sleep() \&\&|' \
         's|!serial_has_input() \&\& !mrble::connected()) {|!mrble::connected()) {|' \
         's|^    mr_ui_tick((uint32_t)now);.*$|    ;|; s|^        if (board_sleep_until(due, s_now)) ++g_sleep_count;.*$|\&\n    mr_ui_tick((uint32_t)now);|'
# W24 ⛔ THE HOOK MUST STAY FEATURE-NEUTRAL, exactly as W13 requires of the config path: no `MR_FEAT_OLED` may appear
#     anywhere near the sleep gate, or `fw_main` has acquired a display dependency. Its control ADDS the guard, because
#     "no `#if` here" cannot be reverted by deletion.
w24() { ! flat1 "$1" | grep -qE 'MR_FEAT_OLED'; }
wchk_in "$FW_MAIN" "W24 fw_main's sleep gate stays feature-neutral (no MR_FEAT_OLED)" \
     w24 's|    if (may_sleep \&\& mr_ui_allows_sleep() \&\&|#if MR_FEAT_OLED\n    if (may_sleep \&\& mr_ui_allows_sleep() \&\&|'
# W25 ⛔⛔ THE DIO1 WAKE IS NOT REPLACED, REORDERED OR REMOVED. §3.1.2: the radio's RTC-domain `ext1` source stays
#     exactly as it was, and whether it COEXISTS with the new digital-domain GPIO source is metal-only. A slice that
#     "simplified" the radio onto GPIO wake would be changing the radio path behind a UI fix.
w25() { flat1 "$1" | grep -qF 'esp_sleep_enable_ext1_wakeup((1ULL << LORA_PIN_DIO1), ESP_EXT1_WAKEUP_ANY_HIGH);'; }
wchk_in "$FW_MAIN" "W25 board_sleep_until still arms DIO1 ext1 wake, unchanged" \
     w25 's|        esp_sleep_enable_ext1_wakeup((1ULL << LORA_PIN_DIO1), ESP_EXT1_WAKEUP_ANY_HIGH);.*|        ;|' \
         's|        esp_sleep_enable_ext1_wakeup((1ULL << LORA_PIN_DIO1), ESP_EXT1_WAKEUP_ANY_HIGH);.*|        gpio_wakeup_enable((gpio_num_t)LORA_PIN_DIO1, GPIO_INTR_HIGH_LEVEL);|'
# W26 ★★ THE NON-OLED ARM. Every profile without a panel must answer `true` — that is what makes this slice inert for
#     them. A stub returning `false` would stop every headless node in the fleet from ever light-sleeping, and no test
#     in this tree compiles that arm.
MR_UI_H="$ROOT/lib/hal/mr_ui.h"
w26() { flat1 "$1" | grep -qF 'inline bool mr_ui_allows_sleep() { return true; }' && \
        flat1 "$1" | grep -qF 'bool mr_ui_allows_sleep();'; }
wchk_in "$MR_UI_H" "W26 the non-OLED hook is an inline TRUE stub and the OLED one is declared" \
     w26 's|inline bool mr_ui_allows_sleep() { return true; }|inline bool mr_ui_allows_sleep() { return false; }|' \
         's|^bool mr_ui_allows_sleep();.*$||'
# W26b ★★ §B200's TWO NEW SEAMS, same property and the same reason. ⛔ THE NON-OLED ARM IS THE ONE THAT CAN SILENTLY
#      STOP THE FLEET SLEEPING: a stub answering anything but `ok` would make every headless XIAO refuse every sleep,
#      and NO test in this tree compiles that arm. The disarm stub must answer `true` for the same reason — the caller
#      COUNTS a false as a hardware failure.
w26b() { flat1 "$1" | grep -qF 'inline MrUiWakeArm mr_ui_arm_button_wake() { return MrUiWakeArm::ok; }' && \
         flat1 "$1" | grep -qF 'inline bool mr_ui_disarm_button_wake() { return true; }' && \
         flat1 "$1" | grep -qF 'MrUiWakeArm mr_ui_arm_button_wake();' && \
         flat1 "$1" | grep -qF 'bool mr_ui_disarm_button_wake();'; }
wchk_in "$MR_UI_H" "W26b the non-OLED arm/disarm stubs are permissive and both OLED seams are declared" \
     w26b 's|inline MrUiWakeArm mr_ui_arm_button_wake() { return MrUiWakeArm::ok; }|inline MrUiWakeArm mr_ui_arm_button_wake() { return MrUiWakeArm::failed; }|' \
          's|inline bool mr_ui_disarm_button_wake() { return true; }|inline bool mr_ui_disarm_button_wake() { return false; }|' \
          's|^MrUiWakeArm mr_ui_arm_button_wake();.*$||' \
          's|^bool mr_ui_disarm_button_wake();.*$||'
# W27 ★★★ THE FAIL-CLOSED PATH, AND IT IS THE MOST IMPORTANT CHECK IN THIS BLOCK. ⛔⛔ RETARGETED BY §B200 — the
#     §B197 form required `s_btn_wake_armed = mrui::enable_button_wake();` at boot, i.e. IT REQUIRED THE DEFECT.
#     Five clauses now, five wrong answers:
#       (a) the lockout latch DEFAULTS to false, i.e. sleeping is allowed until hardware proves otherwise — safe
#           because the arm now happens INSIDE the sleep path and refuses it there (the old boot latch is obsolete);
#       (b) the latch has ONE writer, `latch_sleep_off()`, which returns the EDGE so the line is said once and a
#           broken board cannot flood the console from the per-pass arm;
#       (c) the arm DELEGATES to the board and maps all three verdicts — ⛔ `button_down` must NOT latch, or the
#           first press of the day disables sleep for the boot;
#       (d) each failure is SAID with its own exact line (the bench reads them); and
#       (e) `mr_ui_allows_sleep` REFUSES on the latch before consulting any UI state.
w27() { flat1 "$1" | grep -qF 'bool s_sleep_locked_out = false;' && \
        flat1 "$1" | grep -qF 'static bool latch_sleep_off() { const bool first = !s_sleep_locked_out; s_sleep_locked_out = true; return first; }' && \
        flat1 "$1" | grep -qF 'MrUiWakeArm mr_ui_arm_button_wake() { switch (mrui::arm_button_wake()) { case mrui::WakeArm::armed: return MrUiWakeArm::ok; case mrui::WakeArm::button_down: return MrUiWakeArm::button_down; case mrui::WakeArm::failed: break; } if (latch_sleep_off()) mrcon.println(F("!! OLED button wake unavailable; sleep disabled")); return MrUiWakeArm::failed; }' && \
        flat1 "$1" | grep -qF 'bool mr_ui_disarm_button_wake() { if (mrui::disarm_button_wake()) return true; if (latch_sleep_off()) mrcon.println(F("!! OLED button wake stuck armed; sleep disabled")); return false; }' && \
        flat1 "$1" | grep -qF 'bool mr_ui_allows_sleep() { if (s_sleep_locked_out) return false; return mrui::ui_allows_sleep(s_model, s_input, s_gate); }'; }
wchk_in "$FW_UI" "W27 the per-sleep arm maps all three verdicts, says its failures, and sleep FAILS CLOSED" \
     w27 's|bool              s_sleep_locked_out = false;|bool              s_sleep_locked_out = true;|' \
         's|        case mrui::WakeArm::button_down: return MrUiWakeArm::button_down;|        case mrui::WakeArm::button_down: break;|' \
         's|    switch (mrui::arm_button_wake()) {|    (void)mrui::arm_button_wake(); switch (mrui::WakeArm::armed) {|' \
         's|    if (latch_sleep_off()) mrcon.println(F("!! OLED button wake unavailable; sleep disabled"));|    ;|' \
         's|    if (mrui::disarm_button_wake()) return true;|    (void)mrui::disarm_button_wake(); return true;|' \
         's|    if (s_sleep_locked_out) return false;|    ;|' \
         's|    return mrui::ui_allows_sleep(s_model, s_input, s_gate);|    return true;|' \
         's|static bool latch_sleep_off() { const bool first = !s_sleep_locked_out; s_sleep_locked_out = true; return first; }|static bool latch_sleep_off() { return !s_sleep_locked_out; }|'
# W27b ⛔⛔ THE [[B200]] WIRING CHECK, AND IT IS PURE NEGATIVE SPACE: `mr_ui_init()` MUST NOT ARM ANYTHING. This is the
#      single line §B197 shipped and §B200 removed — an arm at boot is a level-triggered interrupt that outlives every
#      sleep, and the owner reproduced the panic with one long press. Its control ADDS the old line back, because
#      negative space cannot be reverted by deletion; that control is literally the defect.
# ⚠ Scoped to the FUNCTION BODY, not the file: the file must (and does) name the board's arm exactly once, inside the
#   seam `fw_main` calls at the sleep. Scoping it to `mr_ui_init` is what makes "nowhere at boot" the property.
init_body() { sed -n '/^void mr_ui_init()/,/^}/p' "$1" | sed 's://.*::'; }
w27b() { init_body "$1" | grep -qE 'void mr_ui_init\(\)' && ! init_body "$1" | grep -qE 'arm_button_wake|enable_button_wake'; }
wchk_in "$FW_UI" "W27b mr_ui_init() ARMS NOTHING — no boot-time wake arm survives (B200)" \
     w27b 's|    s_model.attach_config(s_cfg);|    (void)mrui::arm_button_wake();\n    s_model.attach_config(s_cfg);|' \
          's|    s_model.attach_config(s_cfg);|    (void)mr_ui_arm_button_wake();\n    s_model.attach_config(s_cfg);|'
# W27c ★ ...and the board's arm is reached from EXACTLY ONE place in the feature layer — the seam. A second caller is
#      how "armed only at the sleep" quietly becomes "armed at the sleep AND somewhere else".
w27c() { [ "$(flat1 "$1" | grep -o 'mrui::arm_button_wake()' | wc -l)" -eq 1 ] && \
         [ "$(flat1 "$1" | grep -o 'mrui::disarm_button_wake()' | wc -l)" -eq 1 ]; }
wchk_in "$FW_UI" "W27c the board's arm/disarm are each called from exactly ONE site" \
     w27c 's|    battery_maybe_sample(now_ms);|    (void)mrui::arm_button_wake();\n    battery_maybe_sample(now_ms);|' \
          's|    battery_maybe_sample(now_ms);|    (void)mrui::disarm_button_wake();\n    battery_maybe_sample(now_ms);|'
# W28 ⛔ `s_painting` STAYS PRIVATE TO THE BOARD TU ([[B198]]'s own correction). The logical page-loop authority is
#     `FrameGate::frame_open()`, which already existed; exporting the board's private latch would give the sleep policy
#     a SECOND page-loop authority that nothing keeps in step with the first.
# ★ TWO CLAUSES: the feature layer never names it, and the canvas header exposes no accessor for it. Its controls ADD
#   rather than delete, because negative space cannot be reverted by deletion.
w28() { ! flat1 "$1" | grep -qE 's_painting|painting\(\)'; }
wchk_in "$FW_UI" "W28 firmware_ui.cpp never reaches for the board-private s_painting" \
     w28 's|    if (s_sleep_locked_out) return false;|    if (s_sleep_locked_out \|\| mrui::s_painting) return false;|'
wchk_in "$BOARD_H" "W28b board_ui.h exposes no page-loop accessor (FrameGate::frame_open is the authority)" \
     w28 's|^WakeArm arm_button_wake();$|WakeArm arm_button_wake();\nbool painting();|'
# ================================================================================================ W29-W31
# ★★★ §B200 — WHERE THE ARM LIVES. This is the fix, and `src/fw_main.cpp` is reachable by no behavioural gate at all,
#     so these are the only automated cover the placement will ever have.
# W29 THE PAIR STRADDLES THE HALT, INSIDE THE RTC GUARD. Five properties in one sequence:
#       (a) the arm is INSIDE `if (rtc_gpio_is_valid_gpio(...))` — ⛔ outside it, a board whose DIO1 is not RTC-capable
#           arms and then neither sleeps nor disarms, which is [[B200]] again on another board;
#       (b) it sits immediately before `esp_light_sleep_start()` — the armed level exists only while the CPU is halted;
#       (c) a verdict other than `ok` RETURNS WITHOUT SLEEPING (nothing was armed, so nothing is owed);
#       (d) ⛔⛔ THE DISARM IS THE **FIRST STATEMENT** AFTER THE HALT RETURNS — not merely "after it" (§R2.2); and
#       (e) its verdict is captured and counted.
# ★★★ (d) IS THE SAFETY PROPERTY AND IT IS WHY THIS CHECK WAS ITSELF RETARGETED 2026-08-15. On a GPIO wake the button
#     is BY DEFINITION still held low — that is what woke the node — so the interval between the halt returning and
#     `gpio_wakeup_disable()` is EXACTLY the window in which the storm condition is live on a RUNNING CPU. The first
#     version of this check tolerated the wake-cause read sitting in that window, and W31 below actively REQUIRED it.
# ⚠ The regex demands ADJACENCY (`esp_light_sleep_start(); const bool disarm_ok =`), with no `.*` between them, so
#   ANY statement interposed there turns it red — which is the whole point of "first".
w29() { flat1 "$1" | grep -qE 'if \(rtc_gpio_is_valid_gpio\(\(gpio_num_t\)LORA_PIN_DIO1\)\) \{.*const MrUiWakeArm arm = mr_ui_arm_button_wake\(\); if \(arm != MrUiWakeArm::ok\) \{.*return false; \} const esp_err_t sleep_rc = esp_light_sleep_start\(\); const bool disarm_ok = mr_ui_disarm_button_wake\(\); if \(!disarm_ok\) \+\+g_wake_disarm_fail;'; }
wchk_in "$FW_MAIN" "W29 the wake is armed immediately BEFORE the halt and disarmed FIRST after it, inside the RTC guard" \
     w29 's|^        const MrUiWakeArm arm = mr_ui_arm_button_wake();$|        const MrUiWakeArm arm = MrUiWakeArm::ok;|' \
         's|^        const bool disarm_ok = mr_ui_disarm_button_wake();$|        const bool disarm_ok = true;|' \
         's|^        if (!disarm_ok) ++g_wake_disarm_fail;.*$|        ;|' \
         's|^        const esp_err_t sleep_rc = esp_light_sleep_start();$|__W29_SLEEP__|; s|^        const bool disarm_ok = mr_ui_disarm_button_wake();$|        const esp_err_t sleep_rc = esp_light_sleep_start();|; s|^__W29_SLEEP__$|        const bool disarm_ok = mr_ui_disarm_button_wake();|' \
         's|^        const MrUiWakeArm arm = mr_ui_arm_button_wake();$|__W29_ARM__|; s|^    if (rtc_gpio_is_valid_gpio((gpio_num_t)LORA_PIN_DIO1)) {.*$|    const MrUiWakeArm arm = mr_ui_arm_button_wake();\n\&|; s|^__W29_ARM__$|        ;|' \
         's|^        if (arm != MrUiWakeArm::ok) {$|        if (false) {|' \
         's|^        const esp_err_t sleep_rc = esp_light_sleep_start();$|\&\n        const esp_sleep_wakeup_cause_t cause2 = esp_sleep_get_wakeup_cause(); (void)cause2;|'
# W30 ★★★ `slept=` MUST NOT LIE. The `++` used to sit BEFORE the call, when reaching the call and halting were the
#     same thing; with a fallible arm they are not, and `slept=` is the field every Part-23 bench check reads. ⇒ the
#     counter is CONDITIONAL on the call's own report, and there is exactly ONE of them.
w30() { flat1 "$1" | grep -qF 'if (board_sleep_until(due, s_now)) ++g_sleep_count;' && \
        [ "$(flat1 "$1" | grep -o '++g_sleep_count' | wc -l)" -eq 1 ]; }
wchk_in "$FW_MAIN" "W30 slept= counts only a sleep that ACTUALLY happened (one conditional ++)" \
     w30 's|^        if (board_sleep_until(due, s_now)) ++g_sleep_count;.*$|        ++g_sleep_count;\n        (void)board_sleep_until(due, s_now);|' \
         's|^        if (board_sleep_until(due, s_now)) ++g_sleep_count;.*$|        ++g_sleep_count;\n\&|' \
         's|^        if (board_sleep_until(due, s_now)) ++g_sleep_count;.*$|        (void)board_sleep_until(due, s_now); ++g_sleep_count;|'
# W31 ⛔⛔ RETARGETED 2026-08-15 (§R2.2) — AND THE OLD VERSION IS THE REASON THIS COMMENT IS LONG. It read:
#       `w31() { … grep -qE 'const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause\(\); if \(!mr_ui_disarm_button_wake\(\)\)' … }`
#     i.e. IT REQUIRED THE WAKE-CAUSE READ TO SIT BETWEEN THE HALT AND THE DISARM — **the instrument ENFORCED the
#     unsafe order**, and would have gone red against the safe one. ★ That is the THIRD time in this arc an instrument
#     pinned the shape it should forbid (after the arm-once W-checks and [[B195]]'s vacuous tripwire), which is why the
#     rule is now written here: when a check encodes an ORDER, ask which end of it is the safety property.
# ★★★ THE ORDER IS: halt returns → DISARM → (refused-sleep bail) → inspect the cause. On a GPIO wake the button is by
#     definition still held low, so every instruction before `gpio_wakeup_disable()` runs with the storm condition
#     live on a running CPU. Diagnostics are never allowed in that window.
# ★★ The counters themselves still answer what bench 23.1(b) could not: with the sleep capped at 1 s the MCU wakes
#    anyway, so only a per-cause tally can say the DIO1 edge (or the button) actually delivered the CPU.
w31() { flat1 "$1" | grep -qE 'const bool disarm_ok = mr_ui_disarm_button_wake\(\); if \(!disarm_ok\) \+\+g_wake_disarm_fail; if \(sleep_rc != ESP_OK\) \{ \+\+g_wake_sleep_fail; return false; \} const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause\(\);' && \
        flat1 "$1" | grep -qF 'if (cause == ESP_SLEEP_WAKEUP_GPIO) ++g_wake_gpio; else if (cause == ESP_SLEEP_WAKEUP_EXT1) ++g_wake_ext1; else if (cause == ESP_SLEEP_WAKEUP_TIMER) ++g_wake_timer;'; }
wchk_in "$FW_MAIN" "W31 the cause is inspected AFTER the disarm (never in the storm window) and tallied per source" \
     w31 's|^        const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();$|__W31_CAUSE__|; s|^        const bool disarm_ok = mr_ui_disarm_button_wake();$|        const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();\n\&|; s|^__W31_CAUSE__$|        ;|' \
         's|^        const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();$|__W31_CAUSE__|; s|^        const esp_err_t sleep_rc = esp_light_sleep_start();$|\&\n        const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();|; s|^__W31_CAUSE__$|        ;|' \
         's|^        if      (cause == ESP_SLEEP_WAKEUP_GPIO)  ++g_wake_gpio;.*$|        ;|' \
         's|(cause == ESP_SLEEP_WAKEUP_EXT1)  ++g_wake_ext1;|(cause == ESP_SLEEP_WAKEUP_EXT1)  ++g_wake_gpio;|'
# ================================================================================================ W32
# W32 ★★★ §R2.1 — `esp_light_sleep_start()` CAN REFUSE, AND A REFUSAL IS NOT A SLEEP. ESP-IDF returns
#     `ESP_ERR_SLEEP_REJECT` (a wake source already asserted / entry rejected) or
#     `ESP_ERR_SLEEP_TOO_SHORT_SLEEP_DURATION` (the interval is below the entry/exit overhead). Discarding the return
#     re-broke truthful `slept=` THROUGH A DIFFERENT DOOR: the rejection would be counted as a sleep and its STALE
#     wake cause tallied as if it were fresh — on a node whose `slept=` is the field every Part-23 bench check reads.
# ★★ THE PROPERTY IS `!= ESP_OK`, NOT A LIST OF CODES, and the two narrowing controls are what make that measurable:
#    each restricts the test to ONE documented rejection and therefore leaves the OTHER counted as a sleep. A single
#    control could not distinguish "handles every non-OK code" from "handles the one code the control happens to use".
# ⛔ And the bail must come BEFORE the cause is read — a stale cause is worse than no cause, because it looks fresh.
w32() { flat1 "$1" | grep -qF 'const esp_err_t sleep_rc = esp_light_sleep_start();' && \
        flat1 "$1" | grep -qF 'if (sleep_rc != ESP_OK) { ++g_wake_sleep_fail; return false; }'; }
wchk_in "$FW_MAIN" "W32 a REFUSED light sleep is counted and returns false, before any cause is read" \
     w32 's|^        const esp_err_t sleep_rc = esp_light_sleep_start();$|        esp_light_sleep_start();|; s|^        if (sleep_rc != ESP_OK) { ++g_wake_sleep_fail; return false; }$|        ;|' \
         's|^        if (sleep_rc != ESP_OK) { ++g_wake_sleep_fail; return false; }$|        (void)sleep_rc;|' \
         's|^        if (sleep_rc != ESP_OK) { ++g_wake_sleep_fail; return false; }$|        if (sleep_rc == ESP_ERR_SLEEP_REJECT) { ++g_wake_sleep_fail; return false; }|' \
         's|^        if (sleep_rc != ESP_OK) { ++g_wake_sleep_fail; return false; }$|        if (sleep_rc == ESP_ERR_SLEEP_TOO_SHORT_SLEEP_DURATION) { ++g_wake_sleep_fail; return false; }|' \
         's|^        if (sleep_rc != ESP_OK) { ++g_wake_sleep_fail; return false; }$|        if (sleep_rc != ESP_OK) ++g_wake_sleep_fail;|' \
         's|^        if (sleep_rc != ESP_OK) { ++g_wake_sleep_fail; return false; }$|        if (sleep_rc != ESP_OK) { return false; }|'
# ================================================================================================ W33
# W33 ★★★★ §B200 ROUND 3 — THE BOOT SCRUB, AND ITS PLACEMENT IS THE PROPERTY. A reset taken WHILE THE WAKE IS ARMED
#     (a panic or watchdog during light sleep) leaves the pin's level interrupt configured with NO disarm ever having
#     run; `RTC_SW_CPU_RST` does not clear it, so the next boot storms as soon as a GPIO ISR exists. Metal
#     discriminated it cleanly: a boot that had slept before a `reboot` PANICKED on a held button, a boot that never
#     slept did not.
# ⛔⛔ THE ORDER IS THE WHOLE CHECK: `fault_wdt_start()` → SCRUB → `g_radio.std_init()`. RadioLib installs the shared
#     GPIO ISR inside `std_init()`, so a scrub after it is a scrub after the storm; and `mr_ui_init()` at the end of
#     `setup()` — the "obvious" home for anything UI — is far too late for the same reason.
# ★ It goes through the EXISTING `mr_ui_disarm_button_wake()` seam (U1: a scrub IS a disarm), so `fw_main` gains no
#   sixth hook and still knows nothing about a button pin or a panel.
# ★ FOUR CONTROLS, each a plausible wrong answer rather than a deletion: scrub removed · scrub moved AFTER the radio
#   init · scrub moved into `mr_ui_init()`-era code (after the radio is up) · ⛔ scrub replaced by an ARM, which is
#   [[B200]]'s ORIGINAL defect written out literally.
w33() { flat1 "$1" | grep -qE 'mrfault::fault_wdt_start\(\);.*const bool wake_scrub_ok = mr_ui_disarm_button_wake\(\);.*g_radio\.std_init'; }
wchk_in "$FW_MAIN" "W33 the boot scrub runs after the WDT arm and BEFORE the radio installs its GPIO ISR" \
     w33 's|^    const bool wake_scrub_ok = mr_ui_disarm_button_wake();.*$|    const bool wake_scrub_ok = true;|' \
         's|^    const bool wake_scrub_ok = mr_ui_disarm_button_wake();.*$|    bool wake_scrub_ok = true;|; s|^    g_radio_ok = ok;$|\&\n    wake_scrub_ok = mr_ui_disarm_button_wake();|' \
         's|^    const bool wake_scrub_ok = mr_ui_disarm_button_wake();.*$|    bool wake_scrub_ok = true;|; s|^    mr_ui_init();.*$|\&\n    wake_scrub_ok = mr_ui_disarm_button_wake();|' \
         's|^    const bool wake_scrub_ok = mr_ui_disarm_button_wake();.*$|    const bool wake_scrub_ok = (mr_ui_arm_button_wake() == MrUiWakeArm::ok);|'
# ================================================================================================ W34
# W34 ★★★★ §B200 ROUND 4 — **ONE TEARDOWN, THREE CALLERS, AND NO SECOND COPY.** This check exists because the bug it
#     forbids has already happened: round 3 added the interrupt-type clear to `disarm_button_wake()` and NOT to the
#     arm's rollback, so one site kept clearing bit 10 alone and recreated the exact residue round 3 removed — while
#     reporting `failed`, i.e. "nothing is armed". ⛔ A behavioural probe CANNOT see this: a duplicated body behaves
#     identically until the day someone edits one copy. Only a structural count can forbid the duplication itself.
# ★ The property is stated as CARDINALITY over code (comments stripped): each of the three platform teardown calls
#   appears EXACTLY ONCE in the whole TU, and the shared helper is named four times (one definition + three callers:
#   the normal disarm and both arm-failure paths).
# ★ FOUR CONTROLS, and each is a real way the copies drift apart: inline the body at the rollback · inline it at the
#   disarm · revert one caller to the bit-10-only call · drop a caller's teardown entirely.
BOARD_CPP="$BOARD/board_ui.cpp"
n_of() { flat1 "$1" | grep -o -F "$2" | wc -l; }
w34() { [ "$(n_of "$1" 'gpio_set_intr_type(')" -eq 1 ] && \
        [ "$(n_of "$1" 'gpio_wakeup_disable(')" -eq 1 ] && \
        [ "$(n_of "$1" 'esp_sleep_disable_wakeup_source(')" -eq 1 ] && \
        [ "$(n_of "$1" 'clear_button_wake_state()')" -eq 4 ]; }
wchk_in "$BOARD_CPP" "W34 ONE shared teardown, three callers — no second copy of the withdrawals" \
     w34 's|^        (void)clear_button_wake_state();$|        (void)gpio_set_intr_type((gpio_num_t)MR_UI_BTN_PIN, GPIO_INTR_DISABLE);\n        (void)gpio_wakeup_disable((gpio_num_t)MR_UI_BTN_PIN);\n        (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);|' \
         's|^bool disarm_button_wake() { return clear_button_wake_state(); }$|bool disarm_button_wake() {\n    const bool t = (gpio_set_intr_type((gpio_num_t)MR_UI_BTN_PIN, GPIO_INTR_DISABLE) == ESP_OK);\n    const bool p = (gpio_wakeup_disable((gpio_num_t)MR_UI_BTN_PIN) == ESP_OK);\n    const esp_err_t r = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);\n    return t \&\& p \&\& (r == ESP_OK \|\| r == ESP_ERR_INVALID_STATE);\n}|' \
         's|^        (void)clear_button_wake_state();$|        (void)gpio_wakeup_disable((gpio_num_t)MR_UI_BTN_PIN);|' \
         's|^        (void)clear_button_wake_state();$|        ;|'
# ================================================================================================ W35-W36
# ★★★ §CHROME-2 / design §8.1 — THE HARD BOUNDARY, NOW THAT THE BOARD HAS DRAWING PRIMITIVES A UI WANTS TO USE.
#     Slice 2 adds `draw_bitmap` / `draw_rect`, and the very next temptation is the one §8.1 names by example: move
#     the icon table down beside them and expose `draw_mail_icon()`. ⛔ That would make the V4 port a rewrite instead
#     of a pin table — the whole reason the seam exists — and NOTHING else in the tree can see it: the board TU is
#     compiled by no native test and no simulator, and a semantic call would build and run perfectly.
# ★ TWO CLAUSES: no UI header may be INCLUDED by either board file, and no SEMANTIC draw entry point may be declared
#   or defined in them. S4a already covers `board_ui.h` x `firmware_ui_model.h`; this widens it to both files and to
#   §CHROME-1's two new headers, which did not exist when S4a was written.
# ★ CONTROLS ADD rather than delete, because negative space cannot be reverted by deletion — and the third control is
#   literally the function §8.1 forbids by name.
# ⚠ §B77: `code_flat` strips comments FIRST. Both board files state this invariant in prose and name all three
#   headers while forbidding them, so a bare grep would read the documentation as the violation — which is exactly
#   how both S4 checks failed on their first writing.
w35() { ! code_flat "$1" | grep -qE '#[[:space:]]*include[^;]*firmware_ui_(model|chrome|icons)' && \
        ! code_flat "$1" | grep -qE 'draw_[a-z_]*_icon[[:space:]]*\('; }
wchk_in "$BOARD_CPP" "W35 board_ui.cpp includes no UI header and declares no semantic icon call (§8.1)" \
     w35 's|#include "board_ui.h"|#include "board_ui.h"\n#include "firmware_ui_icons.h"|' \
         's|#include "board_ui.h"|#include "board_ui.h"\n#include "firmware_ui_model.h"|' \
         's|^void draw_hline(int x, int y, int w)        { s_u8g2.drawHLine(x, y, w); }$|\&\nvoid draw_mail_icon(int x, int y) { s_u8g2.drawXBM(x, y, 7, 7, nullptr); }|'
wchk_in "$BOARD_H" "W36 board_ui.h includes no UI header and declares no semantic icon call (§8.1)" \
     w35 's|#include <cstdint>|#include <cstdint>\n#include "firmware_ui_chrome.h"|' \
         's|#include <cstdint>|#include <cstdint>\n#include "firmware_ui_icons.h"|' \
         's|^void draw_rect(int x, int y, int w, int h);$|\&\nvoid draw_mail_icon(int x, int y);|'
# ================================================================================================ W37
# ★★ §CHROME-2 — THE TWO PRIMITIVES ARE **DECLARED IN THE CANVAS HEADER**, i.e. they are part of the seam rather than
#    file-local helpers. A `static` definition in `board_ui.cpp` would compile, pass every P12 check (the probe
#    includes the same TU)… and then fail to link the moment slice 3's renderer calls them, from the one file no
#    host build compiles. ⇒ pin the declarations, and pin that they carry the byte-pointer form rather than a
#    board-owned type.
w37() { code_flat "$1" | grep -qF 'void draw_bitmap(int x, int y, int w, int h, const uint8_t* bits);' && \
        code_flat "$1" | grep -qF 'void draw_rect(int x, int y, int w, int h);'; }
wchk_in "$BOARD_H" "W37 both §CHROME-2 primitives are declared in the canvas header, in the generic byte form" \
     w37 's|^void draw_bitmap(int x, int y, int w, int h, const uint8_t\* bits);$||' \
         's|^void draw_rect(int x, int y, int w, int h);$||' \
         's|^void draw_bitmap(int x, int y, int w, int h, const uint8_t\* bits);$|void draw_bitmap(int x, int y, int w, int h, const char* bits);|'
# ================================================================================================ W38-W40
# ★★★★ §CHROME-3 — THE THREE PROPERTIES OF THE STRIP'S WIRING THAT **NO BEHAVIOURAL PROBE IN THIS TREE CAN REACH**.
#     Everything else about the strip is measured by `tools/probe_firmware_ui`'s P13 against the real renderer; these
#     three are not, and each is named rather than assumed.
#
# W38 ★★★★ THE 64-BIT HOME AGE IS TAKEN **VERBATIM** FROM THE ACCESSOR. Design §4.2 forbids a 32-bit millisecond age
#     because it re-creates the ~49.7-day wrap this project already fixed once — a four-month-old confirmation would
#     render `13h`, i.e. a stale home link shown as a live one, on a safety device.
# ⛔⛔ AND IT IS **UNREACHABLE BY EVERY OTHER GATE**, WHICH IS THE REASON THIS CHECK EXISTS: `mobile_home_confirmed_ever()`
#     is `_mobile_home_confirmed_ms != 0`, set ONLY by the mobile FSM's RF paths, so no host probe can make the age
#     non-zero at all — the firmware probe reads `--` whatever the arithmetic does. The native battery's X01 pins the
#     FORMATTER's parameter type; nothing pins the PUBLISHER's, and the publisher is where the snapshot's own idiom
#     (`now_ms` is `uint32_t`, `last_dm_age_s` is a `uint32_t` age) invites the cast to be written naturally.
# ★ THREE CONTROLS, and only the first is a deletion: the other two are the two tempting WRONG answers — the explicit
#   32-bit cast, and re-deriving the age from the snapshot's 32-bit clock instead of asking the core.
w38() { code_flat "$1" | grep -qF 's.home_confirm_age_ms  = g_node.mobile_home_confirm_age_ms();' && \
        ! code_flat "$1" | grep -qE 'uint32_t\([[:space:]]*g_node\.mobile_home_confirm_age_ms'; }
wchk_in "$FW_UI" "W38 the 64-bit home confirmation age reaches the snapshot VERBATIM, never recomputed or cast" \
     w38 's|    s.home_confirm_age_ms  = g_node.mobile_home_confirm_age_ms();|    ;|' \
         's|    s.home_confirm_age_ms  = g_node.mobile_home_confirm_age_ms();|    s.home_confirm_age_ms  = uint32_t(g_node.mobile_home_confirm_age_ms());|' \
         's|    s.home_confirm_age_ms  = g_node.mobile_home_confirm_age_ms();|    s.home_confirm_age_ms  = uint64_t(now_ms - uint32_t(g_node.mobile_home_confirm_age_ms()));|'
# W39 ALL FIVE §CHROME-1 FIELDS ARE ACTUALLY PUBLISHED. They were DEFINED one slice ago and held their declared
#     "nothing established" defaults; a projection built from unpublished fields is honest but blind, and four of the
#     five would still render something plausible (`--`, a blank slot) if their assignment were dropped — which is
#     precisely the shape that survives review.
w39() { for f in 'mobile_build         = (MR_FEAT_MOBILE != 0)' 'home_link            = g_node.mobile_home_link()' \
                 'home_confirmed_ever  = g_node.mobile_home_confirmed_ever()' \
                 'team_key_present     = g_node.team_channel_key_present()'; do
          code_flat "$1" | grep -qF "s.$f" || return 1
        done; return 0; }
wchk_in "$FW_UI" "W39 build_snapshot publishes the §CHROME-1 fields from the core's own accessors" \
     w39 's|    s.home_link            = g_node.mobile_home_link();|    ;|' \
         's|    s.team_key_present     = g_node.team_channel_key_present();|    ;|' \
         's|    s.mobile_build         = (MR_FEAT_MOBILE != 0);|    s.mobile_build         = true;|'
# W40 ★★★ §8.3's COMPARISON REFERENCE MOVES **AT THE FREEZE AND NOWHERE ELSE**, and the strip is drawn from the
#     FROZEN copy. The two are one object on purpose (`s_frame_chrome`): a reference updated where a difference is
#     OBSERVED would consume the invalidation without ever drawing it, and — because that same object is what the
#     page loop replays — would tear the strip across the remaining pages of an open frame.
# ⓘ The behavioural half is `probe_firmware_ui`'s P13d (all eight page replays) and P13e (the positive invalidation);
#   this pins the STRUCTURE those two depend on, in the one file no native test compiles.
# ⛔ The fourth clause is negative space: this file must never clear the dirty bit — see W3, which forbids
#   `clear_dirty` outright, and which is what makes §8.3.1's "NEVER clear an existing dirty bit" structural here.
w40() { code_flat "$1" | grep -qF 'mrui::ui_chrome_invalidate(s_model, live_chrome, s_frame_chrome)' && \
        [ "$(code_flat "$1" | grep -o 's_frame_chrome = live_chrome;' | wc -l)" -eq 1 ] && \
        code_flat "$1" | grep -qE 'FrameStep::open:.*s_frame_chrome = live_chrome;.*mrui::begin_frame\(\);' && \
        code_flat "$1" | grep -qF 'draw_frame(s_frame_state, s_frame_snap, s_frame_out, s_frame_cfg, s_frame_chrome)'; }
wchk_in "$FW_UI" "W40 the chrome reference updates ONLY at the freeze, and the strip is drawn from it" \
     w40 's|    (void)mrui::ui_chrome_invalidate(s_model, live_chrome, s_frame_chrome);|    (void)live_chrome;|' \
         's|            s_frame_chrome = live_chrome;.*$|            ;|' \
         's|    (void)mrui::ui_chrome_invalidate(s_model, live_chrome, s_frame_chrome);|    if (mrui::ui_chrome_invalidate(s_model, live_chrome, s_frame_chrome)) s_frame_chrome = live_chrome;|' \
         's|draw_frame(s_frame_state, s_frame_snap, s_frame_out, s_frame_cfg, s_frame_chrome)|draw_frame(s_frame_state, s_frame_snap, s_frame_out, s_frame_cfg, live_chrome)|'
# ================================================================================================ W41-W43
# ★★★★ §CHROME-4 — THE THREE PROPERTIES OF THE RAIL AND THE BODY MIGRATION THAT **NO BEHAVIOURAL PROBE IN THIS TREE
#     CAN REACH**. Everything else about them is measured by `tools/probe_firmware_ui`'s P14 against the real
#     renderer; these three are not, and each is named rather than assumed.
#
# W41 ★★★★ §3.2's *"unavailable slots remain empty and the remaining icons keep the same locations rather than
#     acquiring a second layout"*, AND IT IS STRUCTURAL BECAUSE IT CANNOT BE BEHAVIOURAL HERE. ⛔ NO host variant of
#     `probe_firmware_ui` can compile a `!MR_FEAT_TEAM` `firmware_ui.cpp` against a team-enabled `lib/core`, so the
#     one build where TEAM/SEND are missing (`gateway_heltec`) is reachable only by the LINKER. ⇒ what is pinned is
#     the construction that makes the property true for every mask at once: the slot's y comes from the ENUMERATOR
#     (`NavSlot(i + 1)` -> `rail_slot_y(i)`), and an unavailable slot is `continue`d rather than skipped-and-packed.
#     ★ THREE CONTROLS, and only one is a deletion: the other two are the two tempting wrong answers — pack the
#       available slots with a running counter, and ignore the mask so dead icons are drawn on a build that has no
#       TEAM plane (§3.2's "misleading dead icons", verbatim).
w41() { code_flat "$1" | grep -qF 'const mrui::NavSlot s = mrui::NavSlot(i + 1);' && \
        code_flat "$1" | grep -qF 'if ((c.slots & mrui::slot_bit(s)) == 0) continue;' && \
        code_flat "$1" | grep -qF 'const int y = rail_slot_y(i);' && \
        code_flat "$1" | grep -qF 'if (c.nav == s) mrui::draw_rect(kRailX, y, kRailW, kRailH);'; }
wchk_in "$FW_UI" "W41 the rail indexes its slots by the ENUMERATOR and skips unavailable ones" \
     w41 's|        if ((c.slots \& mrui::slot_bit(s)) == 0) continue;|        ;|' \
         's|        const int y = rail_slot_y(i);|        static int packed = 0; const int y = rail_slot_y(packed++);|' \
         's|        const mrui::NavSlot s = mrui::NavSlot(i + 1);|        const mrui::NavSlot s = c.nav;|'
# W42 ★★★ §7.1 rule 1 — **ONE** `kBodyX` AUTHORITY, AS NEGATIVE SPACE. §13 refuses a partial state in which "icons
#     are drawn over 21-column content", and the shape that produces it is a migration that moved MOST draw sites: a
#     single surviving `mrui::draw_text(0, body_y(...))` puts one line under the rail's icons on one screen, which no
#     count can see and which the firmware probe would only catch if its walk happened to reach that screen.
# ⛔ THE EMERGENCY EXCEPTION IS PINNED IN THE SAME CHECK, because it is the one body that MUST stay at x = 0 (§5.3):
#    a grep that merely forbade `x = 0` would push somebody to "fix" the distress headline into a clipped one.
w42() { ! code_flat "$1" | grep -qE 'draw_text\([[:space:]]*0,[[:space:]]*body_y' && \
        code_flat "$1" | grep -qF 'void body_text(int row, const char* s) { mrui::draw_text(kBodyX, body_y(row), s); }' && \
        code_flat "$1" | grep -qF 'constexpr int kBodyX    = 12;' && \
        code_flat "$1" | grep -qF 'mrui::draw_text(0, kEmgHeadY, head);'; }
wchk_in "$FW_UI" "W42 every ordinary body line goes through the ONE kBodyX authority" \
     w42 's|    body_text(1, "no teammates heard");|    mrui::draw_text(0, body_y(1), "no teammates heard");|' \
         's|void body_text(int row, const char\* s) { mrui::draw_text(kBodyX, body_y(row), s); }|void body_text(int row, const char* s) { mrui::draw_text(0, body_y(row), s); }|' \
         's|    mrui::draw_text(0, kEmgHeadY, head);|    mrui::draw_text(kBodyX, kEmgHeadY, head);|'
# W43 ★★★ §7.3's OTHER HALF, AND IT IS THE ONE §CHROME-4's BRIEF CALLS THE BIGGEST RISK IN THE SLICE: the inbox
#     detail's wrap must move AT THE MODEL, not at the draw origin. `kDetailCols` and the renderer's `kBodyCols` are
#     ONE number — a renderer drawing 19 columns over a model still wrapping at 21 clips every full row AND makes
#     `detail_pages` a lie. The `static_assert` is what makes that a build failure; this check is what stops the
#     assert being deleted by somebody who finds it in the way.
# ⓘ The VALUE 19 is pinned natively (`ui7d-modal:` re-derives 38 chars a page and 7 pages from it); what no native
#   case can see is the renderer's side of the equality, because nothing compiles this file.
w43() { code_flat "$1" | grep -qF 'static_assert(kBodyCols == mrui::kDetailCols,' && \
        code_flat "$1" | grep -qF 'constexpr int kBodyCols = 19;'; }
wchk_in "$FW_UI" "W43 the body width and the model's detail wrap are ONE number (build-enforced)" \
     w43 's|static_assert(kBodyCols == mrui::kDetailCols,|static_assert(kBodyCols == kBodyCols,|' \
         's|constexpr int kBodyCols = 19;    |constexpr int kBodyCols = 21;    |'
# ================================================================================================ W44-W46
# ★★★★ [[B196]] — THE ONE `esp_sleep_pd_config()` CALL, ITS COUNT AND ITS PLACE. `esp_sleep_pd_config` is REF-COUNTED
#     in an ESP-IDF `int16_t refs`, so a call PER SLEEP ATTEMPT overflowed it and `assert(refs >= 0)` panic-rebooted
#     every headless node about every nine hours. The fix asserts the domain ONCE PER BOOT in `setup()`.
# ⛔⛔ AND THIS DEFECT IS INVISIBLE TO EVERY BEHAVIOURAL GATE WE HAVE — native does not compile this arm, the 36 corpus
#     streams never enter it, and neither UI probe drives `fw_main.cpp`'s sleep path. It needs a real ESP32, real IDF
#     state and 32,769 sleeps (bench Part 26). ⇒ what CAN be measured here is the SOURCE SHAPE, and nothing more is
#     claimed for it: these three checks cannot prove the node stops rebooting, only that the tree still has the
#     shape the metal soak validated.
# ★★★ THE COUNT MUST BE OF **CALL EXPRESSIONS IN CODE**, NOT TEXT OCCURRENCES, and that is not a nicety: the required
#     comments at BOTH ends of this fix name `esp_sleep_pd_config` and `ESP_PD_OPTION_OFF` in prose, so a raw
#     `grep -c` over the tree fails the instant the fix is documented — §B77's trap (a check reading its own
#     documentation as the violation), which has already broken two checks in this file.
# ⇒ comments are stripped BY THE COMPILER (`-fpreprocessed -dD -E -P` removes `//` AND `/* */` without needing a
#   single include or macro), and the count is then taken with a call-shaped pattern over the STRIPPED text.
# ⓘ A file whose RAW text does not mention the identifier at all contributes 0 and is not stripped — stripping can
#   only REMOVE occurrences, so raw absence already proves code absence. That also keeps the one vendored file
#   `-fpreprocessed` refuses (`lib/monocypher/src/monocypher.c`, whose macros use escaped newlines) out of the loop
#   without a silent skip: if a file that DOES mention the identifier cannot be stripped, pd_count fails LOUD.
prod_files() {   # every production source/header the firmware compiles — `src/`, `variants/`, `lib/`
  find "$ROOT/src" "$ROOT/variants" "$ROOT/lib" -type f \
       \( -name '*.cpp' -o -name '*.cc' -o -name '*.c' -o -name '*.h' -o -name '*.hpp' \) | sort
}
pd_count() {   # pd_count <ERE> <fw_main-substitute>: matches in COMMENT-STRIPPED production code, with $2 standing
               # in for src/fw_main.cpp so every clause below is mutation-controllable through wchk_in's copy.
  local re=$1 sub=$2 total=0 f p n
  while IFS= read -r f; do
    p=$f; [ "$f" = "$ROOT/src/fw_main.cpp" ] && p=$sub
    grep -qE 'esp_sleep_pd_config|ESP_PD_OPTION' "$p" || continue
    if ! "$CXX" -fpreprocessed -dD -E -P "$p" > "$OUT/pd_strip.i" 2>/dev/null; then
      echo "  pd_count: CANNOT comment-strip $p — the count would be unsound" >&2; printf '9999'; return
    fi
    n=$(grep -oE "$re" "$OUT/pd_strip.i" | wc -l); total=$((total+n))
  done < <(prod_files)
  printf '%s' "$total"
}
PD_CALL_RE='esp_sleep_pd_config[[:space:]]*\('
# W44 THE CARDINALITY. Exactly ONE `ON` call expression in the whole of `src/` + `variants/` + `lib/`, and ZERO
#     `ESP_PD_OPTION_OFF` — the latter forbids a future half-pairing driving `refs` NEGATIVE from the other side
#     (the `OFF` path asserts on the POST-decrement value, so an unmatched `OFF` is the same defect mirrored).
# ★ THREE CONTROLS, and the first one is the defect verbatim: the call restored at its old per-attempt home; the new
#   call deleted (count 0 — a check that only catches duplication would pass over a fix that vanished); and an `OFF`
#   introduced after the halt.
w44() { [ "$(pd_count "$PD_CALL_RE" "$1")" -eq 1 ] && [ "$(pd_count 'ESP_PD_OPTION_OFF' "$1")" -eq 0 ]; }
# ⚠ THE COUNTS ARE PRINTED UNCONDITIONALLY, unlike every other check here, and so is the SWEEP SIZE: a cardinality
#   check whose file list came out empty would report `0` and look like a considered measurement. Two numbers make
#   that visible — how many files were walked, and how many of them name the identifier at all.
pd_naming() { while IFS= read -r f; do grep -qE 'esp_sleep_pd_config|ESP_PD_OPTION' "$f" && echo "$f"; done < <(prod_files); }
echo "  W44 counts: $(prod_files | wc -l) production files walked, $(pd_naming | wc -l) naming the identifier -> $(pd_count "$PD_CALL_RE" "$FW_MAIN") call expression(s) (required 1), $(pd_count 'ESP_PD_OPTION_OFF' "$FW_MAIN") ESP_PD_OPTION_OFF (required 0)"
wchk_in "$FW_MAIN" "W44 exactly ONE esp_sleep_pd_config call in production code, ZERO ESP_PD_OPTION_OFF (got $(pd_count "$PD_CALL_RE" "$FW_MAIN") / $(pd_count 'ESP_PD_OPTION_OFF' "$FW_MAIN"))" \
     w44 's|^        esp_sleep_enable_ext1_wakeup((1ULL << LORA_PIN_DIO1), ESP_EXT1_WAKEUP_ANY_HIGH);.*$|        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);\n\&|' \
         's|^.*B196 return checked.*$|        if (ESP_OK != ESP_OK)|' \
         's|^        const esp_err_t sleep_rc = esp_light_sleep_start();$|\&\n        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);|'
# W45 WHERE IT IS, AND UNDER WHAT. The call sits in `setup()` — scoped by the body extraction, so "once per boot"
#     is the property rather than "somewhere in the file" — inside BOTH compile conditions and under the SAME runtime
#     guard the old site had, with its return CHECKED and said in wording that is NOT panel-specific.
# ⛔ Dropping any of the three conditions is a behaviour change, not a tidy-up: an `MR_NO_POWERSAVE` build must gain
#    no call it never had, only the ESP32 arm ever configured a domain, and a board whose DIO1 cannot wake configures
#    nothing. ⇒ the regex demands them ADJACENTLY (no `.*` anywhere in it), so anything interposed turns it red.
# ★ FIVE CONTROLS, and only one is a deletion: each condition dropped in turn · the return discarded · and the
#   ⛔ OLED-specific wording, which would print a false attribution on `xiao_esp32s3` / `gateway_esp32s3` /
#   `xiao_esp32s3_mobile` — three envs that compile this line and have no panel at all.
setup_code() { sed -n '/^void setup() {$/,/^}$/p' "$1" | sed 's://.*::' | tr '\n' ' ' | tr -s ' '; }
w45() { setup_code "$1" | grep -qF '#if !defined(MR_NO_POWERSAVE) #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3) if (rtc_gpio_is_valid_gpio((gpio_num_t)LORA_PIN_DIO1)) { if (esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON) != ESP_OK) mrcon.println(F("!! RTC sleep power-domain configuration failed")); } #endif #endif'; }
wchk_in "$FW_MAIN" "W45 the ONE call is in setup(), under !MR_NO_POWERSAVE + the ESP32 arm + the rtc_gpio guard, return checked" \
     w45 's|^.*B196 cond 1/3.*$||' \
         's|^.*B196 cond 2/3.*$||' \
         's|^.*B196 cond 3/3.*$|    {|' \
         's|^.*B196 return checked.*$|        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);|' \
         's|!! RTC sleep power-domain configuration failed|!! OLED button wake unavailable; sleep disabled|'
# W46 ⛔⛔ NEGATIVE SPACE, AND IT IS THE HALF THAT ACTUALLY FIXES THE BUG: `board_sleep_until()` MUST NOT ASSERT THE
#     DOMAIN. The next reader's instinct is to "restore it for safety" beside the `ext1` arm — that instinct IS the
#     defect, and it cannot be reverted by deletion, so both controls ADD code.
# ★ CONTROL 2 IS CANDIDATE (b), WHICH THE OWNER DID NOT RULE: a one-shot `static bool` inside this function has the
#   same call count and would satisfy any counting check, so W44 alone cannot see it. The ruled shape is once per boot
#   in the INIT PATH, and this is the clause that says so.
# ⚠ The first clause is what stops the check being vacuous: if the `sed` range ever stops matching (a renamed or
#   re-signatured function), the body comes out EMPTY and a bare "does not contain" would pass over anything. A token
#   known to live in that body must be found before its absence means anything.
sleep_fn_code() { sed -n '/^static bool board_sleep_until(/,/^}$/p' "$1" | sed 's://.*::' | tr '\n' ' ' | tr -s ' '; }
w46() { sleep_fn_code "$1" | grep -qF 'esp_sleep_enable_ext1_wakeup(' && \
        ! sleep_fn_code "$1" | grep -qF 'esp_sleep_pd_config'; }
wchk_in "$FW_MAIN" "W46 board_sleep_until() asserts NO power domain (the per-attempt call is gone, B196)" \
     w46 's|^        esp_sleep_enable_ext1_wakeup((1ULL << LORA_PIN_DIO1), ESP_EXT1_WAKEUP_ANY_HIGH);.*$|        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);\n\&|' \
         's|^        esp_sleep_enable_ext1_wakeup((1ULL << LORA_PIN_DIO1), ESP_EXT1_WAKEUP_ANY_HIGH);.*$|        static bool s_pd_done = false; if (!s_pd_done) { s_pd_done = true; esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON); }\n\&|'
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
