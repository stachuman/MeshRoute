// MeshRoute — test/test_firmware_ui_editor.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ W7 (design r2.27 §5, D4) — THE ONE-BUTTON EDITOR's PURE CORE, driven directly: the repertoire and its rings,
//     E1–E4, insertion / deletion / cursor bounds with canaries at both ends, the all-or-nothing preload, the two-row
//     window and the visible ring / header bytes. The callers (name, team, DM) are `test_firmware_ui_model.cpp`'s.
#include "doctest.h"
#include "firmware_ui_editor.h"
#include <algorithm>          // std::max — the ring rows' worst width
#include <cstdint>
#include <initializer_list>
#include <cstring>
#include <string>

using namespace mrui;

namespace {
// A draft opened for `caller` at `cap`, filled with `text` (each byte through the real insertion).
Draft draft_with(const char* text, uint8_t cap = kEditorMessageCap, DraftCaller caller = DraftCaller::team) {
    Draft d{};
    draft_open(d, caller, cap);
    for (const char* p = text; *p; ++p) CHECK(draft_insert(d, *p));
    return d;
}
std::string bytes_of(const Draft& d) { return std::string(d.bytes, d.len); }
EditorView editor_on(EditorPhase p, uint8_t item = 0) { EditorView v{}; editor_enter(v, p); v.item = item; return v; }
void press(EditorView& v, Draft& d, Gesture g, int n = 1) { for (int i = 0; i < n; ++i) (void)editor_gesture(v, d, g); }
// Walk E1 to `group`, open it, walk to `idx` and choose it — the real gestures, no shortcut.
void type_char(EditorView& v, Draft& d, uint8_t group, uint8_t idx) {
    CHECK(v.phase == EditorPhase::groups);
    for (int i = 0; i < kGroupRingItems && v.item != group; ++i) press(v, d, Gesture::short_press);
    press(v, d, Gesture::double_press);
    CHECK(v.phase == EditorPhase::chars);
    for (int i = 0; i < kCharRingItems && v.item != idx; ++i) press(v, d, Gesture::short_press);
    press(v, d, Gesture::double_press);
}
// The byte's (group, index) in the repertoire.
void type(EditorView& v, Draft& d, const char* text) {
    for (const char* p = text; *p; ++p) {
        uint8_t at = kEditorRepertoireSize;
        for (uint8_t i = 0; i < kEditorRepertoireSize; ++i) if (kEditorRepertoire[i] == *p) at = i;
        CHECK(at < kEditorRepertoireSize);
        if (at >= kEditorRepertoireSize) return;
        type_char(v, d, uint8_t(at / kEditorGroupSize), uint8_t(at % kEditorGroupSize));
    }
}
void to_control(EditorView& v, Draft& d, EditorControl c) {
    if (v.phase == EditorPhase::groups) {
        for (int i = 0; i < kGroupRingItems && v.item != kEditorGroups; ++i) press(v, d, Gesture::short_press);
        press(v, d, Gesture::double_press);
    }
    CHECK(v.phase == EditorPhase::controls);
    for (int i = 0; i < kControlRingItems && v.item != uint8_t(c); ++i) press(v, d, Gesture::short_press);
}
}  // namespace

TEST_CASE("w7-editor-repertoire: exactly 42 bytes, each once, in the D4 order — seven groups of six") {
    CHECK(kEditorRepertoireSize == 42);
    CHECK(kEditorGroups == 7);
    CHECK(kEditorGroupSize == 6);
    const char* want = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,?!-";
    CHECK(std::strlen(want) == 42);
    for (uint8_t i = 0; i < kEditorRepertoireSize; ++i) {
        CAPTURE(i);
        CHECK(kEditorRepertoire[i] == want[i]);
        CHECK(editor_char(uint8_t(i / 6), uint8_t(i % 6)) == want[i]);
        for (uint8_t j = uint8_t(i + 1); j < kEditorRepertoireSize; ++j) CHECK(kEditorRepertoire[i] != kEditorRepertoire[j]);
        // ★ §5.2: every byte is a valid console quoted body byte — printable, never `"` or `\`.
        CHECK(kEditorRepertoire[i] >= 0x20);
        CHECK(kEditorRepertoire[i] < 0x7F);
        CHECK(kEditorRepertoire[i] != '"');
        CHECK(kEditorRepertoire[i] != '\\');
    }
    CHECK(std::string(&kEditorRepertoire[36], 6) == " .,?!-");
    // ⛔ out of range fails closed — NUL is never inserted by any caller
    CHECK(editor_char(7, 0) == '\0');
    CHECK(editor_char(0, 6) == '\0');
    CHECK(editor_in_repertoire('A'));
    CHECK(editor_in_repertoire(' '));
    CHECK(editor_in_repertoire('-'));
    CHECK_FALSE(editor_in_repertoire('a'));
    CHECK_FALSE(editor_in_repertoire('\''));
    CHECK_FALSE(editor_in_repertoire('"'));
    CHECK_FALSE(editor_in_repertoire('\0'));
    CHECK_FALSE(editor_in_repertoire(char(0xBB)));
}

TEST_CASE("w7-editor-rings: each ring's size, its wrap, and the first item on entry") {
    Draft d = draft_with("");
    EditorView v = editor_on(EditorPhase::groups);
    CHECK(kGroupRingItems == 8);
    CHECK(kCharRingItems == 7);
    CHECK(kControlRingItems == 6);
    for (uint8_t i = 1; i <= 8; ++i) { press(v, d, Gesture::short_press); CHECK(v.item == i % 8); }
    CHECK(v.phase == EditorPhase::groups);
    press(v, d, Gesture::short_press, 2);                       // group 3
    press(v, d, Gesture::double_press);
    CHECK(v.phase == EditorPhase::chars);
    CHECK(v.group == 2);
    CHECK(v.item == 0);                                         // ★ entering a ring highlights its first item
    for (uint8_t i = 1; i <= 7; ++i) { press(v, d, Gesture::short_press); CHECK(v.item == i % 7); }
    CHECK(v.phase == EditorPhase::chars);
    EditorView c = editor_on(EditorPhase::controls);
    for (uint8_t i = 1; i <= 6; ++i) { press(c, d, Gesture::short_press); CHECK(c.item == i % 6); }
    CHECK(c.phase == EditorPhase::controls);
    EditorView e = editor_on(EditorPhase::discard);
    CHECK_FALSE(e.primary);                                     // BACK first
    press(e, d, Gesture::short_press); CHECK(e.primary);
    press(e, d, Gesture::short_press); CHECK_FALSE(e.primary);
    CHECK(d.len == 0);                                          // no press wrote anything
}

TEST_CASE("w7-editor-E1: a group opens E2 on its first character; EDIT opens E3 on DEL") {
    Draft d = draft_with("AB");
    for (uint8_t g = 0; g < kEditorGroups; ++g) {
        CAPTURE(g);
        EditorView v = editor_on(EditorPhase::groups, g);
        CHECK(editor_gesture(v, d, Gesture::double_press) == EditorAct::none);
        CHECK(v.phase == EditorPhase::chars);
        CHECK(v.group == g);
        CHECK(v.item == 0);
    }
    EditorView v = editor_on(EditorPhase::groups, kEditorGroups);
    CHECK(editor_gesture(v, d, Gesture::double_press) == EditorAct::none);
    CHECK(v.phase == EditorPhase::controls);
    CHECK(v.item == uint8_t(EditorControl::del));
    CHECK(bytes_of(d) == "AB");
    // ⓘ only short and double act; the long gestures belong to the emergency and never reach the editor's rings
    EditorView w = editor_on(EditorPhase::groups, 3);
    for (Gesture g : {Gesture::none, Gesture::long_arm, Gesture::long_fire, Gesture::long_cancel}) {
        CHECK(editor_gesture(w, d, g) == EditorAct::none);
        CHECK(w.phase == EditorPhase::groups);
        CHECK(w.item == 3);
    }
}

TEST_CASE("w7-editor-E2: a character is inserted AT THE CURSOR and the highlight returns to group 1; BACK keeps its group") {
    Draft d = draft_with("AC");
    d.cursor = 1;                                               // between A and C
    EditorView v = editor_on(EditorPhase::groups);
    type_char(v, d, 0, 1);                                      // B
    CHECK(bytes_of(d) == "ABC");
    CHECK(d.cursor == 2);
    CHECK(v.phase == EditorPhase::groups);
    CHECK(v.item == 0);                                         // ★ after an insertion: group 1, whatever group was used
    type_char(v, d, 5, 5);                                      // 9, from group 6
    CHECK(bytes_of(d) == "AB9C");
    CHECK(v.item == 0);
    CHECK(v.note == EditorNote::none);
    // BACK in E2 returns to E1 on the SAME group, and writes nothing
    v = editor_on(EditorPhase::groups, 4);
    press(v, d, Gesture::double_press);
    CHECK(v.phase == EditorPhase::chars);
    press(v, d, Gesture::short_press, 6);
    CHECK(v.item == kEditorGroupSize);
    press(v, d, Gesture::double_press);
    CHECK(v.phase == EditorPhase::groups);
    CHECK(v.item == 4);
    CHECK(bytes_of(d) == "AB9C");
}

TEST_CASE("w7-editor-E2: a FULL draft writes nothing, shows FULL and still returns to group 1") {
    Draft d = draft_with("ABCD", 4);
    const Draft before = d;
    EditorView v = editor_on(EditorPhase::groups, 2);
    press(v, d, Gesture::double_press);
    press(v, d, Gesture::short_press, 3);
    press(v, d, Gesture::double_press);
    CHECK(v.note == EditorNote::full);
    CHECK(v.phase == EditorPhase::groups);
    CHECK(v.item == 0);
    CHECK(std::memcmp(&before, &d, sizeof d) == 0);             // ⛔ not one byte moved
    // the note clears at the next press, which STILL acts (r2.26): the short moves the highlight
    press(v, d, Gesture::short_press);
    CHECK(v.note == EditorNote::none);
    CHECK(v.item == 1);
    // the message cap is the draft's capacity: 163 inserts, the 164th is FULL
    Draft m{};
    draft_open(m, DraftCaller::dm, kEditorMessageCap);
    for (int i = 0; i < 163; ++i) CHECK(draft_insert(m, char('A' + i % 26)));
    CHECK(m.len == 163);
    CHECK_FALSE(draft_insert(m, 'Z'));
    CHECK(m.len == 163);
    CHECK(m.cursor == 163);
}

TEST_CASE("w7-editor-E3: DEL removes the byte BEFORE the cursor and stays; LEFT/RIGHT stay in 0..len") {
    Draft d = draft_with("ABC");
    EditorView v = editor_on(EditorPhase::controls);
    CHECK(d.cursor == 3);
    press(v, d, Gesture::double_press);                         // DEL
    CHECK(bytes_of(d) == "AB");
    CHECK(d.cursor == 2);
    CHECK(v.item == uint8_t(EditorControl::del));               // stays on DEL
    to_control(v, d, EditorControl::left);
    press(v, d, Gesture::double_press, 5);                      // past the start does nothing
    CHECK(d.cursor == 0);
    CHECK(v.item == uint8_t(EditorControl::left));
    to_control(v, d, EditorControl::del);
    press(v, d, Gesture::double_press);                         // DEL at 0 does nothing
    CHECK(bytes_of(d) == "AB");
    CHECK(d.cursor == 0);
    to_control(v, d, EditorControl::right);
    press(v, d, Gesture::double_press);
    CHECK(d.cursor == 1);
    press(v, d, Gesture::double_press, 5);                      // past the end does nothing
    CHECK(d.cursor == 2);
    CHECK(v.item == uint8_t(EditorControl::right));
    // DEL in the MIDDLE: the byte before the cursor, the tail kept
    Draft m = draft_with("ABCDE");
    m.cursor = 2;
    CHECK(draft_delete(m));
    CHECK(bytes_of(m) == "ACDE");
    CHECK(m.cursor == 1);
}

TEST_CASE("w7-editor-E3: DONE refuses an empty or all-space draft with EMPTY, and asks the caller otherwise") {
    for (const char* t : {"", " ", "   "}) {
        CAPTURE(t);
        Draft d = draft_with(t);
        EditorView v = editor_on(EditorPhase::controls, uint8_t(EditorControl::done));
        CHECK(editor_gesture(v, d, Gesture::double_press) == EditorAct::none);
        CHECK(v.note == EditorNote::empty);
        CHECK(v.phase == EditorPhase::controls);
        CHECK(bytes_of(d) == t);
    }
    Draft d = draft_with(" A ");
    EditorView v = editor_on(EditorPhase::controls, uint8_t(EditorControl::done));
    CHECK(editor_gesture(v, d, Gesture::double_press) == EditorAct::done);
    CHECK(bytes_of(d) == " A ");                                // ⛔ DONE never trims
    CHECK(editor_all_space("  ", 2));
    CHECK(editor_all_space("", 0));
    CHECK_FALSE(editor_all_space(" A", 2));
    CHECK(editor_all_space("  A", 2));                          // counted: only the first two bytes are asked
}

TEST_CASE("w7-editor-E3/E4: DISCARD leaves at once when empty, else confirms with BACK first; BACK and DISCARD land") {
    Draft e = draft_with("");
    EditorView v = editor_on(EditorPhase::controls, uint8_t(EditorControl::discard));
    CHECK(editor_gesture(v, e, Gesture::double_press) == EditorAct::leave);
    Draft d = draft_with("HELLO");
    d.cursor = 2;
    v = editor_on(EditorPhase::controls, uint8_t(EditorControl::discard));
    CHECK(editor_gesture(v, d, Gesture::double_press) == EditorAct::none);
    CHECK(v.phase == EditorPhase::discard);
    CHECK_FALSE(v.primary);                                     // ★ BACK first
    CHECK(editor_gesture(v, d, Gesture::double_press) == EditorAct::none);   // BACK
    CHECK(v.phase == EditorPhase::controls);
    CHECK(v.item == uint8_t(EditorControl::del));              // ★ E3 re-entered on DEL
    CHECK(bytes_of(d) == "HELLO");
    CHECK(d.cursor == 2);                                       // the cursor is kept
    to_control(v, d, EditorControl::discard);
    press(v, d, Gesture::double_press);
    press(v, d, Gesture::short_press);                          // → DISCARD
    CHECK(v.primary);
    CHECK(editor_gesture(v, d, Gesture::double_press) == EditorAct::leave);
    CHECK(d.len == 0);
    CHECK(d.cursor == 0);
    // E3's BACK returns to E1 on group 1
    Draft b = draft_with("X");
    v = editor_on(EditorPhase::controls, uint8_t(EditorControl::back));
    press(v, b, Gesture::double_press);
    CHECK(v.phase == EditorPhase::groups);
    CHECK(v.item == 0);
}

TEST_CASE("w7-editor-lock: a LOCKED draft is never written, deleted, discarded or preloaded") {
    Draft d = draft_with("LOCKED");
    d.locked = true;
    const Draft before = d;
    CHECK_FALSE(draft_insert(d, 'A'));
    CHECK_FALSE(draft_delete(d));
    CHECK_FALSE(draft_preload(d, "NEW", 3));
    EditorView v = editor_on(EditorPhase::discard);
    v.primary = true;
    CHECK(editor_gesture(v, d, Gesture::double_press) == EditorAct::none);
    CHECK(std::memcmp(&before, &d, sizeof d) == 0);
}

TEST_CASE("w7-editor-preload: ALL OR NOTHING — every byte in the repertoire and within the cap, else empty") {
    Draft d{};
    draft_open(d, DraftCaller::name, kEditorNameCap);
    CHECK(draft_preload(d, "STAN 2", 6));
    CHECK(bytes_of(d) == "STAN 2");
    CHECK(d.cursor == 6);                                       // the cursor lands on the next empty cell
    for (const char* bad : {"Stan", "STAN'S", "ST\xC3\x84N", "A\"B", "A\\B"}) {
        CAPTURE(bad);
        Draft n{};
        draft_open(n, DraftCaller::name, kEditorNameCap);
        CHECK_FALSE(draft_preload(n, bad, uint8_t(std::strlen(bad))));
        CHECK(n.len == 0);                                      // ⛔ nothing uppercased, transliterated or dropped
        CHECK(n.cursor == 0);
    }
    Draft longer{};
    draft_open(longer, DraftCaller::name, 4);
    CHECK_FALSE(draft_preload(longer, "ABCDE", 5));             // over the cap: empty, never clipped
    CHECK(longer.len == 0);
    CHECK_FALSE(draft_preload(longer, nullptr, 0));
    CHECK_FALSE(draft_preload(longer, "", 0));
    // counted: a source whose (uncounted) tail is outside the repertoire still preloads its counted bytes
    CHECK(draft_preload(longer, "ABab", 2));
    CHECK(bytes_of(longer) == "AB");
}

TEST_CASE("w7-editor-draft: open and release keep the draft_id sequence; open sets caller and cap") {
    Draft d{};
    d.draft_id = 0xFFFFFFFFu;
    draft_open(d, DraftCaller::dm, kEditorMessageCap);
    CHECK(d.caller == DraftCaller::dm);
    CHECK(d.cap == 163);
    CHECK(d.len == 0);
    CHECK(d.draft_id == 0xFFFFFFFFu);
    CHECK(draft_insert(d, 'A'));
    d.locked = true;
    draft_release(d);
    CHECK(d.caller == DraftCaller::none);
    CHECK(d.len == 0);
    CHECK_FALSE(d.locked);
    CHECK(d.draft_id == 0xFFFFFFFFu);                           // ★ never reset when the text clears
    draft_open(d, DraftCaller::name, 200);
    CHECK(d.cap == kDraftMax);                                  // never above the storage
}

TEST_CASE("w7-editor-canary: every draft operation stays inside the 163 bytes at both ends") {
    struct Guarded { uint32_t lo = 0xC0FFEE11u; Draft d{}; uint32_t hi = 0xC0FFEE22u; } g;
    draft_open(g.d, DraftCaller::team, kEditorMessageCap);
    for (int i = 0; i < 200; ++i) (void)draft_insert(g.d, '#');            // insert at the end, past the cap
    CHECK(g.d.len == 163);
    g.d.cursor = 0;
    CHECK_FALSE(draft_insert(g.d, '@'));                                    // full: nothing moves at the start
    for (int i = 0; i < 200; ++i) (void)draft_delete(g.d);                  // DEL at 0: nothing
    CHECK(g.d.len == 163);
    g.d.cursor = 163;
    for (int i = 0; i < 200; ++i) (void)draft_delete(g.d);                  // DEL from the end, past the start
    CHECK(g.d.len == 0);
    CHECK(g.d.cursor == 0);
    for (int i = 0; i < 200; ++i) { (void)draft_insert(g.d, 'A'); g.d.cursor = 0; }   // insert at the START, to full
    CHECK(g.d.len == 163);
    for (int i = 0; i < 200; ++i) { draft_left(g.d); draft_right(g.d); draft_right(g.d); }
    CHECK(g.d.cursor == 163);
    CHECK(g.lo == 0xC0FFEE11u);
    CHECK(g.hi == 0xC0FFEE22u);
    // a sentinel past `len` is never read into the window or written by an insert in the middle
    Draft s = draft_with("ABC");
    s.bytes[3] = '!'; s.bytes[4] = '!';
    s.cursor = 1;
    CHECK(draft_insert(s, 'X'));
    CHECK(bytes_of(s) == "AXBC");
    CHECK(s.bytes[4] == '!');                                   // the byte past the new end was never touched
    EditorView v = editor_on(EditorPhase::groups);
    char rows[2][kEditorCols + 1];
    std::memset(rows, '?', sizeof rows);
    editor_window(s, v, rows);
    CHECK(std::string(rows[0]) == "AXBC");
}

TEST_CASE("w7-editor-window: grid rows r and r+1, r = max(0, cursor/19 - 1); the cursor's cell is frozen with them") {
    Draft d{};
    draft_open(d, DraftCaller::team, kEditorMessageCap);
    for (int i = 0; i < 163; ++i) CHECK(draft_insert(d, char('A' + (i / 19) % 26)));   // row k is all one letter
    EditorView v = editor_on(EditorPhase::groups);
    char rows[2][kEditorCols + 1];
    struct Case { uint8_t cursor; const char* r0; const char* r1; uint8_t row, col; };
    const Case cases[] = {
        {0,   "AAAAAAAAAAAAAAAAAAA", "BBBBBBBBBBBBBBBBBBB", 0, 0},
        {18,  "AAAAAAAAAAAAAAAAAAA", "BBBBBBBBBBBBBBBBBBB", 0, 18},
        {19,  "AAAAAAAAAAAAAAAAAAA", "BBBBBBBBBBBBBBBBBBB", 1, 0},    // ★ a multiple of 19: column 0 of the next row
        {37,  "AAAAAAAAAAAAAAAAAAA", "BBBBBBBBBBBBBBBBBBB", 1, 18},
        {38,  "BBBBBBBBBBBBBBBBBBB", "CCCCCCCCCCCCCCCCCCC", 1, 0},    // the cursor's row is the LOWER one
        {152, "HHHHHHHHHHHHHHHHHHH", "IIIIIIIIIII",         1, 0},
        {163, "HHHHHHHHHHHHHHHHHHH", "IIIIIIIIIII",         1, 11},   // the next empty cell, after the last byte
    };
    for (const Case& c : cases) {
        CAPTURE(int(c.cursor));
        d.cursor = c.cursor;
        editor_window(d, v, rows);
        CHECK(std::string(rows[0]) == c.r0);
        CHECK(std::string(rows[1]) == c.r1);
        CHECK(v.cursor_row == c.row);
        CHECK(v.cursor_col == c.col);
        CHECK(v.used == 163);
        CHECK(v.cap == 163);
    }
    // a full 19-byte draft with the cursor at its end shows the NEXT (empty) row — the next empty cell is visible
    Draft n = draft_with("ABCDEFGHIJKLMNOPQRS", kEditorNameCap, DraftCaller::name);
    editor_window(n, v, rows);
    CHECK(std::string(rows[0]) == "ABCDEFGHIJKLMNOPQRS");
    CHECK(std::string(rows[1]) == "");
    CHECK(v.cursor_row == 1);
    CHECK(v.cursor_col == 0);
    // E4 shows the FIRST 19 bytes, whatever the cursor
    EditorView e = editor_on(EditorPhase::discard);
    d.cursor = 100;
    editor_window(d, e, rows);
    CHECK(std::string(rows[0]) == "AAAAAAAAAAAAAAAAAAA");
    CHECK(std::string(rows[1]) == "");
}

TEST_CASE("w7-editor-rows: the three rings as visible bytes — `>` on the highlight, `_` for SPACE, widths 14/17/18") {
    char r3[32], r4[32];
    EditorView v = editor_on(EditorPhase::groups);
    editor_ring_rows(r3, sizeof r3, r4, sizeof r4, v);
    CHECK(std::string(r3) == ">ABCDEF GHIJKL");
    CHECK(std::string(r4) == " MNOPQR STUVWX");
    v.item = 6;                                                 // the marks group, then EDIT, wrapping
    editor_ring_rows(r3, sizeof r3, r4, sizeof r4, v);
    CHECK(std::string(r3) == ">_.,?!- EDIT");
    CHECK(std::string(r4) == " ABCDEF GHIJKL");
    v.item = 7;
    editor_ring_rows(r3, sizeof r3, r4, sizeof r4, v);
    CHECK(std::string(r3) == ">EDIT ABCDEF");
    CHECK(std::string(r4) == " GHIJKL MNOPQR");
    size_t worst = 0;
    for (uint8_t i = 0; i < kGroupRingItems; ++i) {
        v.item = i;
        editor_ring_rows(r3, sizeof r3, r4, sizeof r4, v);
        worst = std::max(worst, std::max(std::strlen(r3), std::strlen(r4)));
    }
    CHECK(worst == 14);
    EditorView c = editor_on(EditorPhase::chars, 3);
    c.group = 0;
    editor_ring_rows(r3, sizeof r3, r4, sizeof r4, c);
    CHECK(std::string(r3) == " A B C>D E F BACK");
    CHECK(std::string(r4) == "ADD D");
    c.group = 6; c.item = 0;
    editor_ring_rows(r3, sizeof r3, r4, sizeof r4, c);
    CHECK(std::string(r3) == ">_ . , ? ! - BACK");
    CHECK(std::string(r4) == "ADD SPACE");
    c.item = 6;
    editor_ring_rows(r3, sizeof r3, r4, sizeof r4, c);
    CHECK(std::string(r3) == " _ . , ? ! ->BACK");
    CHECK(std::string(r4) == "BACK TO GROUPS");
    CHECK(std::strlen(r3) == 17);
    EditorView k = editor_on(EditorPhase::controls);
    editor_ring_rows(r3, sizeof r3, r4, sizeof r4, k);
    CHECK(std::string(r3) == ">DEL LEFT RIGHT");
    CHECK(std::string(r4) == " DONE DISCARD BACK");
    CHECK(std::strlen(r4) == 18);
    k.item = 4;
    editor_ring_rows(r3, sizeof r3, r4, sizeof r4, k);
    CHECK(std::string(r3) == " DEL LEFT RIGHT");
    CHECK(std::string(r4) == " DONE>DISCARD BACK");
    EditorView e = editor_on(EditorPhase::discard);
    editor_ring_rows(r3, sizeof r3, r4, sizeof r4, e);
    CHECK(std::string(r3) == ">BACK");
    CHECK(std::string(r4) == " DISCARD");
    e.primary = true;
    editor_ring_rows(r3, sizeof r3, r4, sizeof r4, e);
    CHECK(std::string(r3) == " BACK");
    CHECK(std::string(r4) == ">DISCARD");
    EditorView closed{};
    editor_ring_rows(r3, sizeof r3, r4, sizeof r4, closed);
    CHECK(std::string(r3) == "");
    CHECK(std::string(r4) == "");
}

TEST_CASE("w7-editor-header: caller + used/cap right-aligned in 19 columns; a NOTE owns row 0 alone (r2.27)") {
    char h[32];
    editor_header_line(h, sizeof h, "NAME", 4, 32, nullptr);
    CHECK(std::string(h) == "NAME           4/32");
    editor_header_line(h, sizeof h, "TO TEAM", 45, 163, nullptr);
    CHECK(std::string(h) == "TO TEAM      45/163");
    editor_header_line(h, sizeof h, "TO STAN", 45, 163, nullptr);
    CHECK(std::string(h) == "TO STAN      45/163");
    editor_header_line(h, sizeof h, "TO STANISL\xBB", 163, 163, nullptr);   // the widest DM label: 8 columns
    CHECK(std::string(h) == "TO STANISL\xBB 163/163");
    CHECK(std::strlen(h) == 19);
    editor_header_line(h, sizeof h, "NAME", 0, 32, nullptr);
    CHECK(std::string(h) == "NAME           0/32");
    // ★ the note replaces the WHOLE row — left-aligned, the counter hidden
    editor_header_line(h, sizeof h, "TO STAN", 163, 163, "RECIPIENT CHANGED");
    CHECK(std::string(h) == "RECIPIENT CHANGED");
    editor_header_line(h, sizeof h, "NAME", 32, 32, "FULL");
    CHECK(std::string(h) == "FULL");
    char tiny[4];
    editor_header_line(tiny, sizeof tiny, "NAME", 4, 32, nullptr);
    CHECK(std::strlen(tiny) < sizeof tiny);                     // never past the buffer
}

TEST_CASE("w7-editor-notes: a note clears at the next press, which STILL acts; a binding note becomes `seen`") {
    Draft d = draft_with("AB");
    EditorView v = editor_on(EditorPhase::groups);
    for (EditorNote n : {EditorNote::full, EditorNote::empty, EditorNote::busy}) {
        v.note = n;
        const uint8_t before = v.item;
        press(v, d, Gesture::short_press);
        CHECK(v.note == EditorNote::none);
        CHECK(v.item == uint8_t((before + 1) % kGroupRingItems));
    }
    for (EditorNote n : {EditorNote::team_changed, EditorNote::recipient_changed}) {
        v = editor_on(EditorPhase::groups, 2);
        v.note = n;
        press(v, d, Gesture::double_press);
        CHECK(v.note == EditorNote::binding_seen);
        CHECK(v.phase == EditorPhase::chars);                   // the press still acted
        CHECK(v.group == 2);
    }
    v = editor_on(EditorPhase::groups);
    v.note = EditorNote::binding_seen;
    press(v, d, Gesture::short_press);
    CHECK(v.note == EditorNote::binding_seen);                  // only the binding healing clears it (the model's)
    // a note raised by THIS press survives it (FULL / EMPTY are raised after the clear)
    Draft full = draft_with("AB", 2);
    v = editor_on(EditorPhase::chars);
    v.note = EditorNote::busy;
    press(v, full, Gesture::double_press);
    CHECK(v.note == EditorNote::full);
}

TEST_CASE("w7-editor-trace: typing a word through the real gestures, with a mistake and DEL") {
    Draft d{};
    draft_open(d, DraftCaller::name, kEditorNameCap);
    EditorView v = editor_on(EditorPhase::groups);
    type(v, d, "STAX");
    to_control(v, d, EditorControl::del);
    press(v, d, Gesture::double_press);
    to_control(v, d, EditorControl::back);
    press(v, d, Gesture::double_press);
    type(v, d, "N 7");
    CHECK(bytes_of(d) == "STAN 7");
    to_control(v, d, EditorControl::done);
    CHECK(editor_gesture(v, d, Gesture::double_press) == EditorAct::done);
}
