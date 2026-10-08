// MeshRoute — src/firmware_ui_editor.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ W7 (design r2.27 §5, owner-ruled D4) — THE ONE-BUTTON EDITOR's PURE CORE. One editor supplies bytes to explicit
//     callers (a name, a team message, a direct message); it never saves, edits presets or sends by itself (§5.1).
//     Pure so the native suite drives every ring, transition and bound directly (§B115): this header includes the
//     standard library and `firmware_ui_input.h` ONLY — ⛔ never `firmware_ui_model.h` (the model includes THIS, so
//     the reverse is a cycle) and ⛔ never `firmware_config.h` (Arduino).
// ★★ COUNTED BYTES THROUGHOUT: the draft carries no terminator (163 bytes fill it exactly), so nothing here may scan
//    for a NUL — ⛔ no `strlen`, no allocation. The model owns the ONE draft (§5.6); this file only operates on it.
// DONE here: the 42-byte repertoire and its rings, E1–E4 (§5.4), insertion / deletion / cursor bounds, the
//   all-or-nothing preload (§4.3), the two-row draft window (§5.3) and the ring / header rows as visible bytes.
// NOT here (by unit boundary — [[meshroute-mark-done-vs-missing-in-code]]):
//   - who opened the editor, what DONE and DISCARD return to, the review and the result: firmware_ui_model.h;
//   - the binding of a written message and its gate: firmware_ui_model.h / firmware_ui_send.h;
//   - drawing the rows and the 6x1 cursor underline: src/firmware_ui.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>    // snprintf — the rows are composed HERE so the native suite asserts the visible bytes
#include "firmware_ui_input.h"

namespace mrui {

// ======================================================================== §5.2 — the repertoire and its rings (D4)
// ★★★ EXACTLY 42 BYTES, EACH ONCE, IN THIS ORDER, SEVEN GROUPS OF SIX. Every byte is printable ASCII outside `"` and
//     `\`, so every draft is a valid console quoted body (§5.2) — the composer adds no escaping and needs none.
inline constexpr uint8_t kEditorGroups    = 7;
inline constexpr uint8_t kEditorGroupSize = 6;
inline constexpr uint8_t kEditorRepertoireSize = uint8_t(kEditorGroups * kEditorGroupSize);
inline constexpr char kEditorRepertoire[kEditorRepertoireSize] = {
    'A', 'B', 'C', 'D', 'E', 'F',   'G', 'H', 'I', 'J', 'K', 'L',   'M', 'N', 'O', 'P', 'Q', 'R',
    'S', 'T', 'U', 'V', 'W', 'X',   'Y', 'Z', '0', '1', '2', '3',   '4', '5', '6', '7', '8', '9',
    ' ', '.', ',', '?', '!', '-' };
// The three rings (§5.2). Entering any ring highlights its FIRST item (§5.4's landing rule).
inline constexpr uint8_t kGroupRingItems   = uint8_t(kEditorGroups + 1);      // the seven groups, then EDIT
inline constexpr uint8_t kCharRingItems    = uint8_t(kEditorGroupSize + 1);   // six characters, then BACK
enum class EditorControl : uint8_t { del = 0, left, right, done, discard, back };
inline constexpr uint8_t kControlRingItems = 6;                               // DEL LEFT RIGHT DONE DISCARD BACK

// ======================================================================== §5.3 / §5.6 — the grid and the draft
inline constexpr uint8_t kEditorCols = 19;   // the body's columns; the renderer asserts it equals `kBodyCols`
// ★ `kDraftMax` IS THE LARGEST CALLER CAP (§5.6, §7.1): the shared message limit. The model asserts it against the
//   phrase record's own `mrnv::kUiPresetTextMax` and the name cap against `peer_name_max` — this header may not
//   include either, so the numbers are stated here and PROVEN there.
inline constexpr uint8_t kDraftMax         = 163;
inline constexpr uint8_t kEditorNameCap    = 32;
inline constexpr uint8_t kEditorMessageCap = kDraftMax;

// Who the draft is FOR. The model sets it when it opens the editor; ⛔ the editor never infers it.
enum class DraftCaller : uint8_t { none = 0, name, team, dm };
// ★★★ THE ONE DRAFT (§5.6, owner-ruled D16 + D18) — 176 B / align 4 on every ABI, owned by `UiModel` and nowhere
//     else: ⛔ no second text array, no whole-draft copy in `UiState`, `UiSnapshot` or a stack local.
//   · `bytes` has NO terminator — `len` is the only length;
//   · `draft_id` advances (mod 2^32) each time a review FREEZES the draft and is never reset when the text clears;
//     a request carries the ID it froze and is compared only for EQUALITY, so a wrap is harmless;
//   · `locked` is the CONTENT LOCK: from SEND until the request has been EXECUTED the bytes are immutable.
struct Draft {
    char        bytes[kDraftMax] = {};
    uint8_t     len    = 0;
    uint8_t     cursor = 0;                    // 0..len — a cursor at `len` sits on the next empty cell
    DraftCaller caller = DraftCaller::none;
    uint8_t     cap    = 0;                    // the caller's cap: 32 for a name, 163 for a message
    uint32_t    draft_id = 0;
    bool        locked   = false;
};
static_assert(sizeof(Draft) == 176 && alignof(Draft) == 4, "D16/D18: the shared draft is 176 B / align 4 on every ABI");
// ★★ W8 — A BORROWED, COUNTED, READ-ONLY VIEW of the draft: what the execution gate and the composer read. It owns
//    nothing and is built per execution (⛔ never stored); `bytes` has no terminator — `len` is the length.
struct DraftView {
    const char* bytes    = nullptr;
    uint8_t     len      = 0;
    bool        locked   = false;
    uint32_t    draft_id = 0;
};
inline DraftView draft_view_of(const Draft& d) { return DraftView{d.bytes, d.len, d.locked, d.draft_id}; }

// ======================================================================== the frozen descriptor (§5.3, D16)
// The editor's phase. `groups`..`discard` are E1..E4 (§5.4). ★ W8: `bind` / `relabel` mean a DM editor's header
//   label — and, for `bind`, the binding itself — is owed by the device's capture this tick (the full raw name is a
//   device read); the editor is otherwise on E1, group 1. The `name_*` phases are the name flow's review, its one
//   save request (requested, then taken by the device service) and its result (§4.3); `message_review` /
//   `message_result` say that the shared review / the compose result now shows a WRITTEN message (§7.3, §7.4.1).
enum class EditorPhase : uint8_t { closed = 0, groups, chars, controls, discard, bind, relabel,
                                   name_review, name_requested, name_taken, name_result,
                                   message_review, message_result };
// ★★ THE FIVE NOTES (§5.3, r2.26), each clearing at the next ELIGIBLE press — a lit short or double the emergency
//    overlay does not absorb — and that press still performs its normal action. ⛔ A note never consumes a press.
// ★ `binding_seen` IS NOT A SIXTH NOTE AND IS NEVER DRAWN: it records that a broken binding's note was SHOWN and then
//   cleared, so the tick does not raise it again until the binding heals. While the binding stays broken, DONE
//   raises the note again (§7.4) — that is the only re-show.
enum class EditorNote : uint8_t { none = 0, full, empty, busy, team_changed, recipient_changed, binding_seen };
// The name save's panel answer (§4.3). `src/firmware_ui.cpp` maps `mrfw::RenameResult` onto it EXHAUSTIVELY —
// `saved` and `unchanged` both read NAME SAVED; the other three are named refusals, ⛔ never an NV failure in disguise.
enum class NameResult : uint8_t { none = 0, saved, nv_failed, too_long, bad_name };
// ★★★ THE DESCRIPTOR, 10 B / align 1, frozen with the frame (counted twice: the model's state and the frozen copy).
//   `primary` is the selection of whatever two-choice row is up — CHANGE NAME on My device, SET NAME on the name
//   prompt, SAVE on the name review, DISCARD on E4. ⛔ `false` is always the SAFE choice, and every entry resets it.
struct EditorView {
    EditorPhase phase = EditorPhase::closed;
    uint8_t     group = 0;          // E2's group (and the group E2's BACK returns to)
    uint8_t     item  = 0;          // the highlighted ring item
    uint8_t     used  = 0;          // the draft's length, frozen with the window
    uint8_t     cap   = 0;
    uint8_t     cursor_row = 0;     // 0 or 1 — which of the two visible grid rows holds the cursor
    uint8_t     cursor_col = 0;     // 0..18
    EditorNote  note   = EditorNote::none;
    NameResult  result = NameResult::none;
    bool        primary = false;
};
static_assert(sizeof(EditorView) == 10 && alignof(EditorView) == 1, "D16: the editor descriptor is 10 B / align 1");

// What a press asks of the CALLER. `done`: open the review (the draft is not empty or all spaces). `leave`: return
// to the opener (DISCARD on an empty draft, or E4's DISCARD — the draft is already cleared).
enum class EditorAct : uint8_t { none = 0, done, leave };

// ======================================================================== the bytes
// ⛔ Fails closed: an out-of-range group or index answers NUL, which no caller inserts.
inline char editor_char(uint8_t group, uint8_t idx) {
    if (group >= kEditorGroups || idx >= kEditorGroupSize) return '\0';
    return kEditorRepertoire[group * kEditorGroupSize + idx];
}
inline bool editor_in_repertoire(char c) {
    for (uint8_t i = 0; i < kEditorRepertoireSize; ++i) if (kEditorRepertoire[i] == c) return true;
    return false;
}
// §5.4's DONE refusal: an empty draft or one made only of spaces is not a message and not a name.
inline bool editor_all_space(const char* b, uint8_t len) {
    for (uint8_t i = 0; i < len; ++i) if (b[i] != ' ') return false;
    return true;
}
// A fresh, empty, unlocked draft for `caller`. ★ `draft_id` is KEPT — it is a sequence, never reset (§5.6).
inline void draft_open(Draft& d, DraftCaller caller, uint8_t cap) {
    d.len = 0; d.cursor = 0; d.caller = caller; d.locked = false;
    d.cap = (cap > kDraftMax) ? kDraftMax : cap;
}
// The draft is released: no caller owns it any more. ★ `draft_id` is kept for the same reason.
inline void draft_release(Draft& d) { d.len = 0; d.cursor = 0; d.caller = DraftCaller::none; d.locked = false; }
// ★★★ §4.3 — THE PRELOAD IS ALL OR NOTHING. The draft opens with `src` only if it fits the cap AND every byte is in the
//     repertoire; otherwise it stays EMPTY. ⛔ Nothing is uppercased, transliterated or dropped. The cursor lands on
//     the next empty cell. Answers whether it preloaded.
inline bool draft_preload(Draft& d, const char* src, uint8_t len) {
    if (!src || len == 0 || len > d.cap || d.locked) return false;
    for (uint8_t i = 0; i < len; ++i) if (!editor_in_repertoire(src[i])) return false;
    for (uint8_t i = 0; i < len; ++i) d.bytes[i] = src[i];
    d.len = len; d.cursor = len;
    return true;
}
// ★★ INSERTION AT THE CURSOR (§5.4 E2): the tail moves right by one, the cursor follows the new byte. ⛔ A full
//    draft writes NOTHING (the caller shows FULL); ⛔ a locked draft is never touched.
inline bool draft_insert(Draft& d, char c) {
    if (d.locked || d.len >= d.cap || d.len >= kDraftMax || d.cursor > d.len) return false;
    for (uint8_t i = d.len; i > d.cursor; --i) d.bytes[i] = d.bytes[i - 1];
    d.bytes[d.cursor] = c;
    ++d.len; ++d.cursor;
    return true;
}
// ★★ DEL REMOVES THE BYTE BEFORE THE CURSOR (§5.4 E3); at 0 it does nothing.
inline bool draft_delete(Draft& d) {
    if (d.locked || d.cursor == 0 || d.cursor > d.len) return false;
    for (uint8_t i = uint8_t(d.cursor - 1); i + 1 < d.len; ++i) d.bytes[i] = d.bytes[i + 1];
    --d.len; --d.cursor;
    return true;
}
// LEFT / RIGHT move within 0..len; a move past either end does nothing.
inline void draft_left(Draft& d)  { if (d.cursor > 0) --d.cursor; }
inline void draft_right(Draft& d) { if (d.cursor < d.len) ++d.cursor; }

// ======================================================================== §5.4 — the states
// Entering a ring highlights its first item; the two-choice selection resets to its safe side.
inline void editor_enter(EditorView& v, EditorPhase p) { v.phase = p; v.item = 0; v.primary = false; }
inline bool editor_is_editing(EditorPhase p) {
    return p == EditorPhase::groups || p == EditorPhase::chars || p == EditorPhase::controls ||
           p == EditorPhase::discard;
}
// ★ The press that clears a note still acts (r2.26). A shown binding note becomes `binding_seen` (see `EditorNote`).
inline void editor_clear_note(EditorView& v) {
    switch (v.note) {
        case EditorNote::team_changed:
        case EditorNote::recipient_changed: v.note = EditorNote::binding_seen; return;
        case EditorNote::full:
        case EditorNote::empty:
        case EditorNote::busy:              v.note = EditorNote::none; return;
        case EditorNote::none:
        case EditorNote::binding_seen:      return;
    }
}
// ★★★ E1–E4 FOR ONE ELIGIBLE PRESS. The caller has already applied every priority above the editor — emergency
//     gestures, the waking press, the overlay's absorption — so a press that reaches here is one the operator saw.
inline EditorAct editor_gesture(EditorView& v, Draft& d, Gesture g) {
    if (g != Gesture::short_press && g != Gesture::double_press) return EditorAct::none;
    if (!editor_is_editing(v.phase)) return EditorAct::none;
    editor_clear_note(v);
    const bool dbl = (g == Gesture::double_press);
    switch (v.phase) {
        case EditorPhase::groups:                                              // E1
            if (!dbl) { v.item = uint8_t((v.item + 1) % kGroupRingItems); return EditorAct::none; }
            if (v.item >= kEditorGroups) { editor_enter(v, EditorPhase::controls); return EditorAct::none; }   // EDIT → DEL
            v.group = v.item;
            editor_enter(v, EditorPhase::chars);                               // a group → its first character
            return EditorAct::none;
        case EditorPhase::chars: {                                             // E2
            if (!dbl) { v.item = uint8_t((v.item + 1) % kCharRingItems); return EditorAct::none; }
            if (v.item >= kEditorGroupSize) {                                  // BACK → E1 on the SAME group
                const uint8_t back_to = v.group;
                editor_enter(v, EditorPhase::groups);
                v.item = back_to;
                return EditorAct::none;
            }
            if (!draft_insert(d, editor_char(v.group, v.item))) v.note = EditorNote::full;   // FULL: nothing written
            editor_enter(v, EditorPhase::groups);                              // ★ either way: E1 on group 1
            return EditorAct::none;
        }
        case EditorPhase::controls:                                            // E3
            if (!dbl) { v.item = uint8_t((v.item + 1) % kControlRingItems); return EditorAct::none; }
            switch (EditorControl(v.item)) {
                case EditorControl::del:   (void)draft_delete(d); return EditorAct::none;   // stays on DEL
                case EditorControl::left:  draft_left(d);         return EditorAct::none;
                case EditorControl::right: draft_right(d);        return EditorAct::none;
                case EditorControl::done:
                    if (editor_all_space(d.bytes, d.len)) { v.note = EditorNote::empty; return EditorAct::none; }
                    return EditorAct::done;
                case EditorControl::discard:
                    if (d.len == 0) return EditorAct::leave;                   // nothing to lose: leave at once
                    editor_enter(v, EditorPhase::discard);                     // ★ BACK first
                    return EditorAct::none;
                case EditorControl::back:  editor_enter(v, EditorPhase::groups); return EditorAct::none;
            }
            return EditorAct::none;
        case EditorPhase::discard:                                             // E4
            if (!dbl) { v.primary = !v.primary; return EditorAct::none; }
            if (!v.primary) { editor_enter(v, EditorPhase::controls); return EditorAct::none; }   // BACK → E3 on DEL
            if (d.locked) return EditorAct::none;                              // ⛔ a locked draft is never cleared
            d.len = 0; d.cursor = 0;                                           // DISCARD: cleared, back to the opener
            return EditorAct::leave;
        case EditorPhase::closed:
        case EditorPhase::bind:
        case EditorPhase::relabel:
        case EditorPhase::name_review:
        case EditorPhase::name_requested:
        case EditorPhase::name_taken:
        case EditorPhase::name_result:
        case EditorPhase::message_review:
        case EditorPhase::message_result: return EditorAct::none;
    }
    return EditorAct::none;
}

// ======================================================================== §5.3 — the visible bytes
// ★★★ THE TWO-ROW WINDOW, frozen with the descriptor: grid rows r and r + 1, r = max(0, ⌊cursor/19⌋ − 1), so the
//     cursor's row is the lower one except on the first row, and a cursor at a multiple of 19 sits in column 0 of the
//     next row (the next empty cell is always visible). ⓘ E4 shows the FIRST 19 bytes instead (§5.4).
inline void editor_window(const Draft& d, EditorView& v, char (&rows)[2][kEditorCols + 1]) {
    v.used = d.len; v.cap = d.cap;
    const uint8_t len = (d.len > kDraftMax) ? kDraftMax : d.len;
    const uint8_t cur = (d.cursor > len) ? len : d.cursor;
    const uint8_t crow = uint8_t(cur / kEditorCols);
    const uint8_t top  = (v.phase == EditorPhase::discard) ? uint8_t(0) : (crow > 0 ? uint8_t(crow - 1) : uint8_t(0));
    for (uint8_t r = 0; r < 2; ++r) {
        const unsigned start = unsigned(top + r) * kEditorCols;
        uint8_t n = 0;
        if (!(v.phase == EditorPhase::discard && r == 1) && start < len)
            n = uint8_t((len - start < kEditorCols) ? (len - start) : kEditorCols);
        for (uint8_t k = 0; k < n; ++k) rows[r][k] = d.bytes[start + k];
        rows[r][n] = '\0';
    }
    v.cursor_row = (v.phase == EditorPhase::discard) ? uint8_t(0) : uint8_t(crow - top);
    v.cursor_col = uint8_t(cur % kEditorCols);
}
// The ring's cell for SPACE (§5.3): drawn `_` in the ring rows ONLY, and announced `ADD SPACE`.
inline char editor_ring_glyph(char c) { return c == ' ' ? '_' : c; }
// A group-ring item's label: its six characters, or EDIT.
inline void editor_group_label(char (&out)[kEditorGroupSize + 1], uint8_t item) {
    if (item >= kEditorGroups) { snprintf(out, sizeof out, "EDIT"); return; }
    for (uint8_t i = 0; i < kEditorGroupSize; ++i) out[i] = editor_ring_glyph(editor_char(item, i));
    out[kEditorGroupSize] = '\0';
}
inline const char* editor_control_text(uint8_t item) {
    switch (EditorControl(item)) {
        case EditorControl::del:     return "DEL";
        case EditorControl::left:    return "LEFT";
        case EditorControl::right:   return "RIGHT";
        case EditorControl::done:    return "DONE";
        case EditorControl::discard: return "DISCARD";
        case EditorControl::back:    return "BACK";
    }
    return "";
}
inline constexpr const char* kEditorBackToGroupsText = "BACK TO GROUPS";
// ★★★ ROWS 3–4: THE CURRENT RING, `>` on the highlighted item, every item preceded by its marker cell. ⛔ Items never
//     clip; the worst widths are 14 (groups), 17 (characters) and 18 (controls).
//   · groups: the highlighted item and the next three, wrapping — `>ABCDEF GHIJKL` / ` MNOPQR STUVWX`;
//   · characters: all seven on row 3 — ` A B C>D E F BACK`; row 4 announces `ADD D` / `ADD SPACE`, or reads
//     `BACK TO GROUPS` with BACK highlighted;
//   · controls: `>DEL LEFT RIGHT` / ` DONE DISCARD BACK`.
inline void editor_ring_rows(char* r3, std::size_t c3, char* r4, std::size_t c4, const EditorView& v) {
    if (!r3 || c3 == 0 || !r4 || c4 == 0) return;
    r3[0] = '\0'; r4[0] = '\0';
    switch (v.phase) {
        case EditorPhase::groups: {
            char a[kEditorGroupSize + 1], b[kEditorGroupSize + 1], c[kEditorGroupSize + 1], e[kEditorGroupSize + 1];
            const uint8_t i = uint8_t(v.item % kGroupRingItems);
            editor_group_label(a, i);
            editor_group_label(b, uint8_t((i + 1) % kGroupRingItems));
            editor_group_label(c, uint8_t((i + 2) % kGroupRingItems));
            editor_group_label(e, uint8_t((i + 3) % kGroupRingItems));
            snprintf(r3, c3, ">%s %s", a, b);
            snprintf(r4, c4, " %s %s", c, e);
            return;
        }
        case EditorPhase::chars: {
            std::size_t n = 0;
            for (uint8_t i = 0; i < kCharRingItems && n + 1 < c3; ++i) {
                const char m = (i == v.item) ? '>' : ' ';
                const int w = (i < kEditorGroupSize)
                    ? snprintf(r3 + n, c3 - n, "%c%c", m, editor_ring_glyph(editor_char(v.group, i)))
                    : snprintf(r3 + n, c3 - n, "%cBACK", m);
                if (w < 0) break;
                n += std::size_t(w);
                if (n >= c3) { n = c3 - 1; break; }
            }
            r3[n] = '\0';
            if (v.item >= kEditorGroupSize) { snprintf(r4, c4, "%s", kEditorBackToGroupsText); return; }
            const char ch = editor_char(v.group, v.item);
            if (ch == ' ') snprintf(r4, c4, "ADD SPACE");
            else           snprintf(r4, c4, "ADD %c", ch);
            return;
        }
        case EditorPhase::controls:
            snprintf(r3, c3, "%cDEL%cLEFT%cRIGHT", v.item == 0 ? '>' : ' ', v.item == 1 ? '>' : ' ',
                     v.item == 2 ? '>' : ' ');
            snprintf(r4, c4, "%cDONE%cDISCARD%cBACK", v.item == 3 ? '>' : ' ', v.item == 4 ? '>' : ' ',
                     v.item == 5 ? '>' : ' ');
            return;
        case EditorPhase::discard:                                             // E4: BACK first
            snprintf(r3, c3, "%cBACK", v.primary ? ' ' : '>');
            snprintf(r4, c4, "%cDISCARD", v.primary ? '>' : ' ');
            return;
        case EditorPhase::closed:
        case EditorPhase::bind:
        case EditorPhase::relabel:
        case EditorPhase::name_review:
        case EditorPhase::name_requested:
        case EditorPhase::name_taken:
        case EditorPhase::name_result:
        case EditorPhase::message_review:
        case EditorPhase::message_result: return;
    }
}
inline constexpr const char* kEditorDiscardHead = "DISCARD DRAFT?";
// ★★★ ROW 0: the caller part and `used/cap` right-aligned in 19 columns — `NAME           4/32`,
//     `TO TEAM      45/163`, `TO STAN      45/163`. ★ r2.27 (W7W8R-1): A NOTE OWNS ROW 0 ALONE, left-aligned, and the
//     counter is hidden — `RECIPIENT CHANGED` is 17 cells and no note fits beside `163/163`. `note` is the note's
//     text, or nullptr for none (the model owns the note lexemes — two of them are W6's).
inline void editor_header_line(char* out, std::size_t cap, const char* caller, uint8_t used, uint8_t dcap,
                               const char* note) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (note) { snprintf(out, cap, "%s", note); return; }
    char cnt[8];
    const int nc = snprintf(cnt, sizeof cnt, "%u/%u", unsigned(used), unsigned(dcap));
    if (nc <= 0 || nc >= int(sizeof cnt)) return;
    const int w = int(kEditorCols) - nc;                     // the caller part's columns, left-aligned
    snprintf(out, cap, "%-*.*s%s", w, w, caller ? caller : "", cnt);
}

}  // namespace mrui
