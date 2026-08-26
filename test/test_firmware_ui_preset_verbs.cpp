// MeshRoute — test/test_firmware_ui_preset_verbs.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-10/UI-11 slice P2 — the `ui preset` verb family, its THREE NDJSON records and the boot diagnosis
// (`src/firmware_ui_preset_verbs.h`). Authorities: the parent design
// docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md §3.2.3 (the grammar + the three records, VERBATIM)
// and docs/superpowers/specs/2026-08-25-ui10-11-preset-catalog-spec.md §2 (the owner's `busy` table).
//
// ★★★ WHY EVERY RECORD IS COMPARED AS A **BYTE STRING** AND NOT FIELD BY FIELD: these lines are a published
//     companion contract (`ios-companion/INBOX_SYNC_CONTRACT.md`), so a re-ordered field, a renamed key or a
//     `1` where the design wrote `true` is a BREAKING CHANGE that no field-wise assertion would notice. The
//     design's own three lines are quoted in `kRecEmergency` / the end record / the six reasons below, and a
//     re-wording has to disagree with something visible.
//
// ★★ AND THE WRITE COUNTS ARE STILL THE MEASUREMENT, exactly as P1's suite argues: a verdict is a value the
//    implementation CHOOSES, a write count is a CONSEQUENCE. Every `busy` row below asserts `saves == 0` AND
//    `loads == 0` — ⛔ never merely that the answer read `busy`.
//
// ⛔ THE LIMIT OF THE CLAIM, unchanged from P1's: the store is a FAKE and the sink is a recorder. No NVS/LittleFS
//    write, no flash WEAR ([[B193]]), no real USB and no real BLE. The verbs over a real transport and the boot
//    lines on a really-corrupt store are METAL-ONLY (M2).
#include <doctest.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include "firmware_ui_preset_verbs.h"

namespace {

using mrfw::PresetErr;
using mrfw::PresetKind;
using mrfw::PresetVerdict;

// ---- the COUNTING store (P1's suite's shape, U3) -----------------------------------------------------------------
struct FakePresetStore : mrfw::IUiPresetStore {
    mrnv::UiPresetBlob rec{};
    mrnv::UiPresetRead state = mrnv::UiPresetRead::ok;
    int  loads = 0, saves = 0;
    bool save_ok = true;
    mrnv::UiPresetRead load(mrnv::UiPresetBlob& out) override {
        ++loads;
        if (state != mrnv::UiPresetRead::ok) { std::memset(&out, 0xA5, sizeof out); return state; }
        out = rec;
        return mrnv::UiPresetRead::ok;
    }
    bool save(const mrnv::UiPresetBlob& b) override {
        ++saves;
        if (!save_ok) return false;
        rec = b; state = mrnv::UiPresetRead::ok;
        return true;
    }
};
struct FakeGate : mrfw::IEmergencyGate {
    bool active = false;
    bool emergency_active() const override { return active; }
};

// ---- the recording sinks -----------------------------------------------------------------------------------------
// ★ `Rec` is the DIRECT sink — what USB's `mrcon` sees: every `line()` written straight through.
struct Rec : mrfw::IPresetLines {
    char   buf[4096] = {};
    size_t len = 0;
    int    lines = 0;
    void line(const char* s, size_t n) override {
        CHECK(len + n + 1 < sizeof buf);            // ⛔ a capture that silently truncated would measure nothing
        if (len + n + 1 >= sizeof buf) return;
        std::memcpy(buf + len, s, n); len += n; buf[len] = '\0'; ++lines;
    }
    void reset() { len = 0; lines = 0; buf[0] = '\0'; }
};
// ★★ `BleRec` MODELS `LineSink` (src/dispatch_sink.h) — the sink the BLE transport passes to the SAME
//    `dispatch(line,len,Print&)`: it accumulates bytes and SHIPS ON '\n'. Driving the same emitter through it and
//    requiring (a) the identical byte stream and (b) the identical number of SHIPPED lines is the one-dispatch
//    proof this layer can carry: a record that lost its terminator would fuse two companion events into one BLE
//    notification, and a transport-specific bound would drop one.
struct BleRec : mrfw::IPresetLines {
    char   ship[4096] = {};
    size_t shipped = 0;
    int    flushes = 0;
    char   pend[512] = {};
    size_t pend_len = 0;
    void line(const char* s, size_t n) override {
        // ⓘ The two capacity guards are asked ONCE PER CALL, not per byte: a per-byte `CHECK` would add thousands of
        //   assertions that measure the HARNESS rather than the property, and an inflated count is exactly what makes
        //   a battery's derived baseline unreadable.
        CHECK(pend_len + n < sizeof pend);
        CHECK(shipped + pend_len + n + 1 < sizeof ship);
        if (pend_len + n >= sizeof pend || shipped + pend_len + n + 1 >= sizeof ship) return;
        for (size_t i = 0; i < n; ++i) {
            pend[pend_len++] = s[i];
            if (s[i] == '\n') {
                std::memcpy(ship + shipped, pend, pend_len);
                shipped += pend_len; ship[shipped] = '\0';
                pend_len = 0; ++flushes;
            }
        }
    }
};

// A fixture: a live catalog over a fake store that STARTS from the compiled defaults, as a booted device does.
struct Fix {
    FakePresetStore   st;
    FakeGate          gate;
    mrfw::PresetCatalog cat{st, gate};
    mrfw::PresetDiag  diag;
    Rec               out;
    Fix() { mrfw::preset_defaults(st.rec); st.state = mrnv::UiPresetRead::ok; cat.begin(); zero(); }
    void zero() { st.loads = 0; st.saves = 0; out.reset(); }
    bool run(const char* line) { return mrfw::preset_verb(cat, diag, line, std::strlen(line), out); }
};

// The design's own first record, quoted: the compiled emergency default (`I'm in danger`, location on).
const char* const kRecEmergency =
    "{\"ev\":\"ui_preset\",\"slot\":\"emergency\",\"enabled\":true,\"text\":\"I'm in danger\",\"location\":true}\n";
const char* const kRecDm1 =
    "{\"ev\":\"ui_preset\",\"slot\":\"dm1\",\"enabled\":true,\"text\":\"Are you OK?\",\"location\":false}\n";
const char* const kRecDm3Disabled =
    "{\"ev\":\"ui_preset\",\"slot\":\"dm3\",\"enabled\":false,\"text\":\"\",\"location\":false}\n";
const char* const kRecChannel1 =
    "{\"ev\":\"ui_preset\",\"slot\":\"channel1\",\"enabled\":true,\"text\":\"Got your message\",\"location\":false}\n";
const char* const kEndDefaults =
    "{\"ev\":\"ui_presets_end\",\"capacity\":17,\"dm_active\":2,\"channel_active\":2,\"generation\":1}\n";

}  // namespace

// ======================================================================== (10) THE THREE RECORDS, BYTE FOR BYTE
TEST_CASE("P2 the three NDJSON records are byte-exact (design §3.2.3)") {
    char b[mrfw::kPresetLineMax];
    mrnv::UiPresetBlob d{};
    mrfw::preset_defaults(d);

    // ---- `ui_preset`, the design's own example line, field for field and in its order.
    size_t n = mrfw::write_ui_preset(b, sizeof b, mrfw::kPresetEmergency, d.slot[mrfw::kPresetEmergency]);
    CHECK(n == std::strlen(kRecEmergency));
    CHECK(std::string(b) == std::string(kRecEmergency));
    n = mrfw::write_ui_preset(b, sizeof b, mrfw::kPresetDmFirst, d.slot[mrfw::kPresetDmFirst]);
    CHECK(std::string(b) == std::string(kRecDm1));
    // ★ A DISABLED SLOT RENDERS `""` AND `false` — canonical zeroing seen from the wire side.
    n = mrfw::write_ui_preset(b, sizeof b, 3, d.slot[3]);
    CHECK(std::string(b) == std::string(kRecDm3Disabled));
    n = mrfw::write_ui_preset(b, sizeof b, mrfw::kPresetChannelFirst, d.slot[mrfw::kPresetChannelFirst]);
    CHECK(std::string(b) == std::string(kRecChannel1));

    // ---- `ui_presets_end`: capacity, BOTH actives, and the generation.
    n = mrfw::write_ui_presets_end(b, sizeof b, 2, 2, 1);
    CHECK(std::string(b) == std::string(kEndDefaults));
    n = mrfw::write_ui_presets_end(b, sizeof b, 8, 0, 4294967295u);
    CHECK(std::string(b) ==
          std::string("{\"ev\":\"ui_presets_end\",\"capacity\":17,\"dm_active\":8,\"channel_active\":0,"
                      "\"generation\":4294967295}\n"));
    CHECK(n > 0);

    // ---- `ui_preset_err`: THE SIX REASONS, each spelled exactly as the design's alternation lists them.
    struct { PresetErr e; const char* word; } six[] = {
        { PresetErr::bad_slot,     "bad_slot"     },
        { PresetErr::bad_text,     "bad_text"     },
        { PresetErr::bad_location, "bad_location" },
        { PresetErr::mandatory,    "mandatory"    },
        { PresetErr::busy,         "busy"         },
        { PresetErr::store,        "store"        },
    };
    for (const auto& r : six) {
        mrfw::write_ui_preset_err(b, sizeof b, r.e);
        CHECK(std::string(b) == std::string("{\"ev\":\"ui_preset_err\",\"reason\":\"") + r.word + "\"}\n");
    }
    // ⛔ AND THE SIX ARE SIX: the reason set is the design's published alternation, so a SEVENTH spelling reaching
    //    a companion is a contract break. `PresetErr` carries `none` + the six + the `count` fence.
    CHECK(static_cast<int>(PresetErr::count) == 7);
}

// ======================================================================== (10) `list` = 17 + end, STABLE ORDER
TEST_CASE("P2 list emits all 17 records in stable slot order incl. disabled, then ui_presets_end") {
    Fix f;
    CHECK(f.run("preset list"));
    CHECK(f.out.lines == mrnv::kUiPresets + 1);          // ★ 17 records + the end record, ⛔ never the enabled 4
    CHECK(f.st.saves == 0);                              // a read verb writes NOTHING

    // The order is the STABLE SLOT order and every slot is present — asserted by walking the captured stream and
    // requiring each `"slot":"<token>"` to appear exactly once, in index order.
    const char* p = f.out.buf;
    for (uint8_t i = 0; i < mrnv::kUiPresets; ++i) {
        char tok[16]; mrfw::preset_slot_token(i, tok, sizeof tok);
        char needle[32]; std::snprintf(needle, sizeof needle, "\"slot\":\"%s\",", tok);
        const char* hit = std::strstr(p, needle);
        CHECK(hit != nullptr);
        if (!hit) break;
        p = hit + std::strlen(needle);
    }
    // ★ THE DISABLED SLOTS ARE IN IT — the pin §3.2.3 states outright ("including disabled slots"), because an
    //   editor that cannot see `dm3` cannot turn it on.
    CHECK(std::strstr(f.out.buf, kRecDm3Disabled) != nullptr);
    CHECK(std::strstr(f.out.buf, kRecEmergency)   != nullptr);
    // ★ ...AND THE END RECORD IS LAST, with the capacity, both actives and the generation.
    const size_t endlen = std::strlen(kEndDefaults);
    CHECK(f.out.len >= endlen);
    if (f.out.len >= endlen) CHECK(std::string(f.out.buf + f.out.len - endlen) == std::string(kEndDefaults));

    // ★★ THE TWO ACTIVE COUNTS ARE MADE UNEQUAL ON PURPOSE — over the compiled defaults they are BOTH 2, so a
    //    dm/channel SWAP in the end record would be invisible. Enabling one more DM makes the pair 3/2, which is
    //    the only shape in which "the right count is in the right field" is a measurement.
    f.zero();
    CHECK(f.run("preset set dm3 loc=off \"third\""));
    f.zero();
    CHECK(f.run("preset list"));
    CHECK(std::strstr(f.out.buf, "\"dm_active\":3,\"channel_active\":2,") != nullptr);
    CHECK(f.out.lines == mrnv::kUiPresets + 1);          // ⛔ still SEVENTEEN + the end record, gaps and all
    // ...and the generation moved with the change, which is the equality token P3's frozen frame seals.
    CHECK(std::strstr(f.out.buf, "\"generation\":2}") != nullptr);
}

// ======================================================================== (10) MUTATING VERBS RETURN THE RECORD
TEST_CASE("P2 a mutating verb answers with the RESULTING record; reset all answers with the full list") {
    Fix f;
    // `set` -> ONE record, for the slot it changed, carrying the NEW words. ⛔ not a dump.
    CHECK(f.run("preset set dm3 loc=on \"meet at the hut\""));
    CHECK(f.out.lines == 1);
    CHECK(f.st.saves == 1);
    CHECK(std::string(f.out.buf) ==
          std::string("{\"ev\":\"ui_preset\",\"slot\":\"dm3\",\"enabled\":true,\"text\":\"meet at the hut\","
                      "\"location\":true}\n"));

    // An IDENTICAL re-set: still ONE record (the verb succeeded), and ⛔ ZERO further writes (the wear guard).
    f.zero();
    CHECK(f.run("preset set dm3 loc=on \"meet at the hut\""));
    CHECK(f.out.lines == 1);
    CHECK(f.st.saves == 0);
    CHECK(std::strstr(f.out.buf, "\"slot\":\"dm3\"") != nullptr);

    // `clear` -> ONE record, showing the slot DISABLED and emptied.
    f.zero();
    CHECK(f.run("preset clear dm3"));
    CHECK(f.out.lines == 1);
    CHECK(std::string(f.out.buf) == std::string(kRecDm3Disabled));

    // `reset <slot>` -> ONE record.
    f.zero();
    CHECK(f.run("preset set dm1 loc=off \"changed\""));
    f.zero();
    CHECK(f.run("preset reset dm1"));
    CHECK(f.out.lines == 1);
    CHECK(std::string(f.out.buf) == std::string(kRecDm1));

    // ★ `reset all` -> THE FULL LIST (17 + end), because there is no ONE slot it changed.
    f.zero();
    CHECK(f.run("preset set channel4 loc=off \"x\""));
    f.zero();
    CHECK(f.run("preset reset all"));
    CHECK(f.out.lines == mrnv::kUiPresets + 1);
    CHECK(std::strstr(f.out.buf, kRecEmergency) != nullptr);
    CHECK(std::strstr(f.out.buf, "\"slot\":\"channel4\",\"enabled\":false") != nullptr);

    // ⛔ `clear emergency` -> `mandatory`, and the emergency slot is UNTOUCHED.
    f.zero();
    CHECK(f.run("preset clear emergency"));
    CHECK(f.out.lines == 1);
    CHECK(std::string(f.out.buf) ==
          std::string("{\"ev\":\"ui_preset_err\",\"reason\":\"mandatory\"}\n"));
    CHECK(f.st.saves == 0);
    CHECK(f.cat.slot(mrfw::kPresetEmergency).enabled == 1);
    // ⓘ ...while `reset emergency` IS allowed: restoring the compiled phrase is a TEXT EDIT, not a disable.
    f.zero();
    CHECK(f.run("preset reset emergency"));
    CHECK(std::string(f.out.buf) == std::string(kRecEmergency));
}

// ======================================================================== (10) THE `busy` TABLE, ROW BY ROW
TEST_CASE("P2 the busy table (spec §2): an ACTIVE emergency answers busy to EVERY mutating verb, no-ops included") {
    Fix f;
    // A well-formed change first, so the "no-op" row below is a REAL no-op over a real record.
    CHECK(f.run("preset set dm4 loc=off \"hello\""));
    const mrnv::UiPresetBlob before = f.st.rec;

    f.gate.active = true;
    struct { const char* line; } rows[] = {
        { "preset set dm4 loc=off \"hello\"" },   // ★ THE RULED ROW: a NO-OP set is `busy` too
        { "preset set dm5 loc=on \"new\"" },
        { "preset clear dm4" },
        { "preset clear emergency" },             // ⛔ `busy` OUTRANKS `mandatory` — the gate is the first question
        { "preset reset dm4" },
        { "preset reset emergency" },
        { "preset reset all" },
    };
    for (const auto& r : rows) {
        f.zero();
        CHECK(f.run(r.line));
        CHECK(f.out.lines == 1);
        CHECK(std::string(f.out.buf) == std::string("{\"ev\":\"ui_preset_err\",\"reason\":\"busy\"}\n"));
        CHECK(f.st.saves == 0);      // ⛔ ZERO writes
        CHECK(f.st.loads == 0);      // ⛔ ZERO loads — the gate is asked BEFORE the store is touched
    }
    // ⛔ The durable record did not move on any row.
    CHECK(std::memcmp(&before, &f.st.rec, sizeof before) == 0);

    // ★ `list` is NOT a mutating verb and is NOT busy: reading the catalog during an alarm is harmless and useful.
    f.zero();
    CHECK(f.run("preset list"));
    CHECK(f.out.lines == mrnv::kUiPresets + 1);

    // ...and the moment the series ends, the same no-op answers `unchanged` (its record), with zero writes.
    f.gate.active = false;
    f.zero();
    CHECK(f.run("preset set dm4 loc=off \"hello\""));
    CHECK(f.out.lines == 1);
    CHECK(std::strstr(f.out.buf, "\"slot\":\"dm4\"") != nullptr);
    CHECK(f.st.saves == 0);
}

// ======================================================================== (10) `bad_location`'s PRODUCER
TEST_CASE("P2 loc= takes EXACTLY on|off — every third value is bad_location, with zero writes") {
    Fix f;
    const char* bad[] = {
        "preset set dm1 loc=maybe \"hi\"",
        "preset set dm1 loc=1 \"hi\"",
        "preset set dm1 loc=ON \"hi\"",
        "preset set dm1 loc= \"hi\"",
        "preset set dm1 loc=onn \"hi\"",
        "preset set dm1 on \"hi\"",              // the term is not a `loc=` term at all
    };
    for (const char* line : bad) {
        f.zero();
        CHECK(f.run(line));
        CHECK(f.out.lines == 1);
        CHECK(std::string(f.out.buf) ==
              std::string("{\"ev\":\"ui_preset_err\",\"reason\":\"bad_location\"}\n"));
        CHECK(f.st.saves == 0);
        CHECK(f.st.loads == 0);                  // ⛔ the catalog is never even asked
    }
    // The two accepted spellings, and they mean opposite things.
    bool v = false;
    CHECK(mrfw::preset_parse_loc("loc=on", 6, v));  CHECK(v == true);
    CHECK(mrfw::preset_parse_loc("loc=off", 7, v)); CHECK(v == false);
    f.zero();
    CHECK(f.run("preset set dm1 loc=on \"hi\""));
    CHECK(std::strstr(f.out.buf, "\"location\":true") != nullptr);
    f.zero();
    CHECK(f.run("preset set dm1 loc=off \"hi\""));
    CHECK(std::strstr(f.out.buf, "\"location\":false") != nullptr);
}

// ======================================================================== bad_slot / bad_text / store
TEST_CASE("P2 the remaining reasons: bad_slot, bad_text and an unreadable store") {
    Fix f;
    const char* bad_slots[] = {
        "preset set dm0 loc=on \"hi\"", "preset set dm9 loc=on \"hi\"", "preset set dm10 loc=on \"hi\"",
        "preset set dm01 loc=on \"hi\"", "preset set channel0 loc=on \"hi\"", "preset set channel9 loc=on \"hi\"",
        "preset set nonsense loc=on \"hi\"", "preset clear dm9", "preset reset channel9",
        "preset clear emergencyx",
    };
    for (const char* line : bad_slots) {
        f.zero();
        CHECK(f.run(line));
        CHECK(std::string(f.out.buf) == std::string("{\"ev\":\"ui_preset_err\",\"reason\":\"bad_slot\"}\n"));
        CHECK(f.st.saves == 0);
    }
    const char* bad_texts[] = {
        "preset set dm1 loc=on \"\"",                       // empty
        "preset set dm1 loc=on \"   \"",                    // all spaces
        "preset set dm1 loc=on \"123456789012345678\"",     // 18 bytes — OQ-A's bound is 17
        "preset set dm1 loc=on unquoted",                   // not a quoted term
        "preset set dm1 loc=on \"unterminated",             // no closing quote
    };
    for (const char* line : bad_texts) {
        f.zero();
        CHECK(f.run(line));
        CHECK(std::string(f.out.buf) == std::string("{\"ev\":\"ui_preset_err\",\"reason\":\"bad_text\"}\n"));
        CHECK(f.st.saves == 0);
    }
    // ...and exactly 17 is ACCEPTED (the bound is inclusive).
    f.zero();
    CHECK(f.run("preset set dm1 loc=on \"12345678901234567\""));
    CHECK(std::strstr(f.out.buf, "\"text\":\"12345678901234567\"") != nullptr);

    // ★ AN UNREADABLE STORE: every mutating verb answers `store` with ⛔ ZERO writes — never a blind rewrite of a
    //   possibly-intact record.
    FakePresetStore st2; FakeGate g2; mrfw::PresetCatalog c2{st2, g2}; mrfw::PresetDiag d2; Rec o2;
    st2.state = mrnv::UiPresetRead::io_failed;
    c2.begin();
    st2.saves = 0;
    CHECK(mrfw::preset_verb(c2, d2, "preset set dm1 loc=on \"hi\"", 26, o2));
    CHECK(std::string(o2.buf) == std::string("{\"ev\":\"ui_preset_err\",\"reason\":\"store\"}\n"));
    CHECK(st2.saves == 0);
    // ...and a save that FAILS reports `store` too (the sixth reason covers both, by the design's own set).
    FakePresetStore st3; FakeGate g3; mrfw::PresetCatalog c3{st3, g3}; mrfw::PresetDiag d3; Rec o3;
    mrfw::preset_defaults(st3.rec); c3.begin();
    st3.save_ok = false;
    CHECK(mrfw::preset_verb(c3, d3, "preset set dm1 loc=on \"hi\"", 26, o3));
    CHECK(std::string(o3.buf) == std::string("{\"ev\":\"ui_preset_err\",\"reason\":\"store\"}\n"));
    // ⛔ NOTHING WAS PUBLISHED: the live catalog still holds the compiled default.
    CHECK(std::memcmp(c3.slot(mrfw::kPresetDmFirst).text, "Are you OK?", 11) == 0);
}

// ======================================================================== THE GRAMMAR ITSELF
TEST_CASE("P2 the grammar: an unknown sub-verb or a trailing token is REFUSED, never silently run") {
    Fix f;
    const char* not_this_grammar[] = {
        "preset",                       // no sub-verb
        "preset frobnicate",            // unknown sub-verb
        "preset list all",              // ⛔ a trailing token is refused, not read as `list`
        "preset clear",                 // missing argument
        "preset reset",
        "preset set dm1",               // too few terms
        "preset set dm1 loc=on",
        "preset set dm1 loc=on \"hi\" extra",
        "preset clear dm1 extra",
        "preset reset all extra",
        "presets list",                 // not the family
        "",
    };
    for (const char* line : not_this_grammar) {
        f.zero();
        CHECK(f.run(line) == false);    // ⇒ the caller prints the usage line
        CHECK(f.out.lines == 0);        // ⛔ and NOTHING was emitted as NDJSON
        CHECK(f.st.saves == 0);
    }
    // The slot token round-trips for all seventeen, and nothing else parses.
    for (uint8_t i = 0; i < mrnv::kUiPresets; ++i) {
        char tok[16]; const size_t n = mrfw::preset_slot_token(i, tok, sizeof tok);
        CHECK(mrfw::preset_slot_of_token(tok, n) == static_cast<long>(i));
    }
    CHECK(mrfw::preset_slot_of_token("emergency1", 10) == -1);
    CHECK(mrfw::preset_slot_of_token("dm", 2) == -1);
    CHECK(mrfw::preset_slot_of_token("channel", 7) == -1);
    CHECK(mrfw::preset_slot_of_token("", 0) == -1);
}

// ======================================================================== (10) USB AND BLE BYTE-AGREE
TEST_CASE("P2 USB and BLE byte-agree: ONE emitter, two sink shapes, identical streams and identical line counts") {
    // ★ The two transports differ ONLY in the sink object `dispatch(line,len,Print&)` is handed — USB the global
    //   `mrcon`, BLE a `LineSink` that ships on '\n' (fw_main.cpp:549). `BleRec` models the latter; requiring the
    //   re-assembled stream AND the shipped-line count to match the direct capture proves both halves: the bytes
    //   are the same, and every record is a WHOLE '\n'-terminated line, so the streaming transport can never fuse
    //   two companion events into one notification or drop a long one.
    const char* lines[] = {
        "preset list",
        "preset set channel8 loc=on \"12345678901234567\"",   // the WIDEST record this file can produce
        "preset clear channel8",
        "preset set dm1 loc=maybe \"x\"",                     // an error record
        "preset reset all",
    };
    for (const char* cmd : lines) {
        FakePresetStore stA, stB; FakeGate gA, gB;
        mrfw::preset_defaults(stA.rec); mrfw::preset_defaults(stB.rec);
        mrfw::PresetCatalog cA{stA, gA}, cB{stB, gB};
        mrfw::PresetDiag dA, dB;
        cA.begin(); cB.begin();
        Rec usb; BleRec ble;
        const bool ra = mrfw::preset_verb(cA, dA, cmd, std::strlen(cmd), usb);
        const bool rb = mrfw::preset_verb(cB, dB, cmd, std::strlen(cmd), ble);
        CHECK(ra == rb);
        CHECK(ble.pend_len == 0);                       // ⛔ no partial line left un-shipped
        CHECK(usb.len == ble.shipped);
        CHECK(std::memcmp(usb.buf, ble.ship, usb.len) == 0);
        CHECK(usb.lines == ble.flushes);
    }
}

// ======================================================================== (10) THE FOUR BOOT-LINE STATES
TEST_CASE("P2 the boot restore: the four storage states each drive the ruled line (or none), with zero writes") {
    struct { mrnv::UiPresetRead st; const char* expect; } arms[] = {
        { mrnv::UiPresetRead::ok,        nullptr },                        // ★ loaded — say NOTHING
        { mrnv::UiPresetRead::absent,    nullptr },                        // ★ a first boot is SILENT
        { mrnv::UiPresetRead::invalid,   mrfw::kPresetInvalidLine  },
        { mrnv::UiPresetRead::io_failed, mrfw::kPresetIoFailedLine },
    };
    for (const auto& a : arms) {
        FakePresetStore st; FakeGate g; mrfw::PresetCatalog cat{st, g}; mrfw::PresetDiag diag; Rec out;
        mrfw::preset_defaults(st.rec);
        st.state = a.st;
        const mrnv::UiPresetRead got = mrfw::preset_boot_restore(cat, diag, out);
        CHECK(got == a.st);
        CHECK(st.saves == 0);                    // ⛔ the boot NEVER writes, not even to repair
        if (!a.expect) {
            CHECK(out.lines == 0);               // ⛔ never a blank line either
            CHECK(out.len == 0);
        } else {
            CHECK(out.lines == 1);
            CHECK(std::string(out.buf) == std::string(a.expect) + "\n");
        }
        // ...and the catalog is USABLE on every arm: the compiled defaults run whenever the record did not load.
        CHECK(cat.slot(mrfw::kPresetEmergency).enabled == 1);
    }

    // ★★ THE RETAINED DIAGNOSIS: an `invalid` boot keeps saying so on `cfg` — until a successful durable mutation
    //    rewrites the complete canonical record, which is the owner's ruled repair.
    FakePresetStore st; FakeGate g; mrfw::PresetCatalog cat{st, g}; mrfw::PresetDiag diag; Rec out;
    st.state = mrnv::UiPresetRead::invalid;
    mrfw::preset_boot_restore(cat, diag, out);
    CHECK(diag.line() == mrfw::kPresetInvalidLine);
    CHECK(mrfw::preset_verb(cat, diag, "preset set dm3 loc=off \"ok now\"", 31, out));
    CHECK(st.saves == 1);
    CHECK(diag.line() == nullptr);               // repaired ⇒ the warning stops being true and stops printing
    // ⛔ ...whereas an `io_failed` store can never be repaired from here: every mutation returns `store` with zero
    //    writes, so there is no `ok` verdict to clear the warning, by construction.
    FakePresetStore st2; FakeGate g2; mrfw::PresetCatalog cat2{st2, g2}; mrfw::PresetDiag diag2; Rec out2;
    st2.state = mrnv::UiPresetRead::io_failed;
    mrfw::preset_boot_restore(cat2, diag2, out2);
    CHECK(mrfw::preset_verb(cat2, diag2, "preset set dm3 loc=off \"ok now\"", 31, out2));
    CHECK(st2.saves == 0);
    CHECK(diag2.line() == mrfw::kPresetIoFailedLine);
}

// ======================================================================== THE RESIDENT COST (spec §5)
TEST_CASE("P2 the resident cost of the ONE live catalog is measured, not assumed") {
    // ★ P2 is where the ruled no-stack placement is PAID: `preset_catalog()` holds one `PresetCatalog` in `.bss`.
    //   The figure is asserted so a future member cannot grow it silently; the per-board RAM delta is QG's.
    CHECK(sizeof(mrnv::UiPresetBlob) == 372);
    CHECK(sizeof(mrfw::PresetCatalog) >= 3 * sizeof(mrnv::UiPresetBlob));
    CHECK(sizeof(mrfw::PresetCatalog) <= 3 * sizeof(mrnv::UiPresetBlob) + 32);
    // ⓘ The line buffer is a STACK local of the emitters and is bounded by the widest record (98 B) — 160 B in the
    //   8 KB console/BLE task, i.e. the same order as every existing `char b[160]` formatter, and ⛔ nowhere near
    //   the 744-B catalog temporary the stack gate refused.
    CHECK(mrfw::kPresetLineMax == 160);
    char b[mrfw::kPresetLineMax];
    mrnv::UiPresetBlob d{}; mrfw::preset_defaults(d);
    mrfw::preset_slot_put(d.slot[mrnv::kUiPresets - 1], true, true, "12345678901234567", 17);
    const size_t widest = mrfw::write_ui_preset(b, sizeof b, mrnv::kUiPresets - 1, d.slot[mrnv::kUiPresets - 1]);
    CHECK(widest > 0);                                   // ⇒ the widest record FITS (0 would mean it overflowed)
    CHECK(widest < mrfw::kPresetLineMax);
}
