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
// ⓘ W6 (D9): `dm3` is a compiled default now, so the defaults' first DISABLED slot is `dm4` (was `dm3`).
const char* const kRecDm4Disabled =
    "{\"ev\":\"ui_preset\",\"slot\":\"dm4\",\"enabled\":false,\"text\":\"\",\"location\":false}\n";
const char* const kRecChannel1 =
    "{\"ev\":\"ui_preset\",\"slot\":\"channel1\",\"enabled\":true,\"text\":\"Got your message\",\"location\":false}\n";
// ⓘ W6 (D14 + D9): `text_max` after `capacity`, and the compiled actives are 3 / 4 (was no `text_max`, 2 / 2).
const char* const kEndDefaults =
    "{\"ev\":\"ui_presets_end\",\"capacity\":17,\"text_max\":163,\"dm_active\":3,\"channel_active\":4,\"generation\":1}\n";
// A `list` page's end record: the same fields, then `page` and `pages` (D14's exact order).
std::string end_page(uint8_t dm, uint8_t ch, unsigned gen, unsigned page) {
    char b[160];
    std::snprintf(b, sizeof b, "{\"ev\":\"ui_presets_end\",\"capacity\":17,\"text_max\":163,\"dm_active\":%u,"
                  "\"channel_active\":%u,\"generation\":%u,\"page\":%u,\"pages\":5}\n", dm, ch, gen, page);
    return b;
}

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
    // ★ A DISABLED SLOT RENDERS `""` AND `false` — canonical zeroing seen from the wire side (W6: `dm4`, was `dm3`).
    n = mrfw::write_ui_preset(b, sizeof b, 4, d.slot[4]);
    CHECK(std::string(b) == std::string(kRecDm4Disabled));
    n = mrfw::write_ui_preset(b, sizeof b, mrfw::kPresetChannelFirst, d.slot[mrfw::kPresetChannelFirst]);
    CHECK(std::string(b) == std::string(kRecChannel1));

    // ---- `ui_presets_end`: capacity, `text_max` (W6), BOTH actives, and the generation — `reset all`'s form.
    n = mrfw::write_ui_presets_end(b, sizeof b, 3, 4, 1);
    CHECK(std::string(b) == std::string(kEndDefaults));
    n = mrfw::write_ui_presets_end(b, sizeof b, 8, 0, 4294967295u);
    CHECK(std::string(b) ==
          std::string("{\"ev\":\"ui_presets_end\",\"capacity\":17,\"text_max\":163,\"dm_active\":8,\"channel_active\":0,"
                      "\"generation\":4294967295}\n"));
    CHECK(n > 0);
    // ★ W6 (D14): a `list` page's end record appends `page` then `pages`, AFTER the generation — exact bytes.
    n = mrfw::write_ui_presets_end(b, sizeof b, 8, 0, 4294967295u, 5);
    CHECK(std::string(b) == end_page(8, 0, 4294967295u, 5));
    CHECK(n > 0);
    // ★ `capacity` and `text_max` are asserted INDEPENDENTLY: the slot count and the byte limit are two numbers.
    CHECK(mrnv::kUiPresets == 17);
    CHECK(mrnv::kUiPresetTextMax == 163);

    // ---- `ui_preset_err`: THE SEVEN REASONS (W6: `bad_page`, D14), each spelled exactly as the contract lists them.
    struct { PresetErr e; const char* word; } six[] = {
        { PresetErr::bad_slot,     "bad_slot"     },
        { PresetErr::bad_text,     "bad_text"     },
        { PresetErr::bad_location, "bad_location" },
        { PresetErr::mandatory,    "mandatory"    },
        { PresetErr::busy,         "busy"         },
        { PresetErr::store,        "store"        },
        { PresetErr::bad_page,     "bad_page"     },
    };
    for (const auto& r : six) {
        mrfw::write_ui_preset_err(b, sizeof b, r.e);
        CHECK(std::string(b) == std::string("{\"ev\":\"ui_preset_err\",\"reason\":\"") + r.word + "\"}\n");
    }
    // ⛔ AND THE SEVEN ARE SEVEN: the reason set is the published alternation, so an EIGHTH spelling reaching a
    //    companion is a contract break. `PresetErr` carries `none` + the seven + the `count` fence (W6; was six / 7).
    CHECK(static_cast<int>(PresetErr::count) == 8);
}

// ======================================================================== (10) `list` = PAGES of 4 + end, STABLE ORDER
// ★★★★ W6 (owner-ruled D14; [[B475]]) — REWRITTEN IN PLACE, and the WITHDRAWN SHAPE IS KEPT VISIBLE: this case pinned
//      *"`list` emits all 17 records ... then ui_presets_end"* (`lines == kUiPresets + 1`). At 163-byte phrases that
//      reply outgrows the 2048-B console stage, so `list [<page>]` answers ONE page. ★ The property the case exists for
//      is UNCHANGED, and it is measured ACROSS the five pages: every slot, disabled ones included, in stable order,
//      exactly once — "an editor that cannot see `dm4` cannot turn it on".
TEST_CASE("P2 list emits all 17 records in stable slot order incl. disabled — W6: across FIVE pages of four, each ending ui_presets_end") {
    Fix f;
    std::string all;
    for (unsigned page = 1; page <= mrfw::kPresetPages; ++page) {
        CAPTURE(page);
        char cmd[32]; std::snprintf(cmd, sizeof cmd, "preset list %u", page);
        f.zero();
        CHECK(f.run(cmd));
        const unsigned recs = (page < mrfw::kPresetPages) ? mrfw::kPresetPageSize
                                                          : mrnv::kUiPresets - (mrfw::kPresetPages - 1) * mrfw::kPresetPageSize;
        CHECK(f.out.lines == int(recs + 1));             // ★ its records + ONE end record (page 5: `channel8` alone)
        CHECK(f.st.saves == 0);                          // a read verb writes NOTHING
        const std::string end = end_page(3, 4, 1, page);
        CHECK(f.out.len >= end.size());
        if (f.out.len >= end.size()) CHECK(std::string(f.out.buf + f.out.len - end.size()) == end);   // ★ LAST
        all.append(f.out.buf, f.out.len - (f.out.len >= end.size() ? end.size() : 0));
    }
    // The order is the STABLE SLOT order and every slot is present EXACTLY ONCE across the pages — asserted by walking
    // the concatenated records and requiring each `"slot":"<token>"` to appear once, in index order.
    const char* p = all.c_str();
    for (uint8_t i = 0; i < mrnv::kUiPresets; ++i) {
        char tok[16]; mrfw::preset_slot_token(i, tok, sizeof tok);
        char needle[32]; std::snprintf(needle, sizeof needle, "\"slot\":\"%s\",", tok);
        const char* hit = std::strstr(p, needle);
        CHECK(hit != nullptr);
        if (!hit) break;
        CHECK(std::strstr(hit + 1, needle) == nullptr);  // ★ exactly once — no slot on two pages
        p = hit + std::strlen(needle);
    }
    // ★ THE DISABLED SLOTS ARE IN IT — the pin §3.2.3 states outright ("including disabled slots").
    CHECK(all.find(kRecDm4Disabled) != std::string::npos);
    CHECK(all.find(kRecEmergency)   != std::string::npos);
    // ★ A BARE `list` IS PAGE 1, byte for byte.
    f.zero();
    CHECK(f.run("preset list"));
    const std::string bare(f.out.buf, f.out.len);
    f.zero();
    CHECK(f.run("preset list 1"));
    CHECK(std::string(f.out.buf, f.out.len) == bare);

    // ★★ THE GENERATION IS THE RESTART TOKEN: with no change between two page reads they report EQUAL generations,
    //    and a `set` between them moves the later page's — which is what tells a companion to start again (D14).
    //    ⓘ The two active counts are unequal over the D9 defaults (3 / 4) and the change keeps them unequal (3 / 5),
    //    so a dm/channel SWAP in the end record is a measurement, not a coincidence.
    f.zero();
    CHECK(f.run("preset list 2"));
    CHECK(std::strstr(f.out.buf, "\"generation\":1,\"page\":2,") != nullptr);
    f.zero();
    CHECK(f.run("preset set channel5 loc=off \"fifth\""));
    f.zero();
    CHECK(f.run("preset list 3"));
    CHECK(std::strstr(f.out.buf, "\"dm_active\":3,\"channel_active\":5,") != nullptr);
    CHECK(std::strstr(f.out.buf, "\"generation\":2,\"page\":3,\"pages\":5}") != nullptr);
}

// ★★★ W6 (D14): `bad_page` — every page token that is not a canonical 1..5, and a second token, answer ONE error
//     record; nothing is loaded or written. ⛔ It REPLACES the usage-line answer a token after `list` used to get.
TEST_CASE("w6-bad_page: a non-canonical page token or a second token answers bad_page, reading and writing nothing") {
    Fix f;
    for (const char* line : { "preset list 0", "preset list 6", "preset list 9", "preset list 01", "preset list +1",
                              "preset list all", "preset list x", "preset list 1 2", "preset list 5 extra" }) {
        CAPTURE(line);
        f.zero();
        CHECK(f.run(line) == true);                      // ★ the family ANSWERED (NDJSON), ⛔ not the usage line
        CHECK(f.out.lines == 1);
        CHECK(std::string(f.out.buf) == std::string("{\"ev\":\"ui_preset_err\",\"reason\":\"bad_page\"}\n"));
        CHECK(f.st.saves == 0);
        CHECK(f.st.loads == 0);
    }
    // ...and the five canonical pages each answer records, never the error.
    for (const char* line : { "preset list 1", "preset list 2", "preset list 3", "preset list 4", "preset list 5" }) {
        f.zero();
        CHECK(f.run(line));
        CHECK(std::strstr(f.out.buf, "bad_page") == nullptr);
    }
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

    // ★ `reset all` -> THE FULL LIST (17 + end, UNPAGED — D14 leaves it unchanged), because there is no ONE slot it
    //   changed. ⓘ W6 (D9): the wearer-enabled slot it reverts is `channel5` (was `channel4`, now a default).
    f.zero();
    CHECK(f.run("preset set channel5 loc=off \"x\""));
    f.zero();
    CHECK(f.run("preset reset all"));
    CHECK(f.out.lines == mrnv::kUiPresets + 1);
    CHECK(std::strstr(f.out.buf, kRecEmergency) != nullptr);
    CHECK(std::strstr(f.out.buf, "\"slot\":\"channel5\",\"enabled\":false") != nullptr);
    CHECK(std::strstr(f.out.buf, "\"page\"") == nullptr);   // ⛔ no page fields on `reset all`

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
    //   ⓘ W6 (D14): a bare `list` is page 1 — four records + the end (was all 17 + the end).
    f.zero();
    CHECK(f.run("preset list"));
    CHECK(f.out.lines == mrfw::kPresetPageSize + 1);

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
    // ⓘ W6 (D7): the overlength arm is T + 1 = 164 bytes (was 18, OQ-A's 17 + 1) — built, not typed.
    const std::string over = "preset set dm1 loc=on \"" + std::string(mrnv::kUiPresetTextMax + 1, 'o') + "\"";
    const char* bad_texts[] = {
        "preset set dm1 loc=on \"\"",                       // empty
        "preset set dm1 loc=on \"   \"",                    // all spaces
        over.c_str(),                                       // 164 bytes — the bound is T = 163
        "preset set dm1 loc=on unquoted",                   // not a quoted term
        "preset set dm1 loc=on \"unterminated",             // no closing quote
    };
    for (const char* line : bad_texts) {
        f.zero();
        CHECK(f.run(line));
        CHECK(std::string(f.out.buf) == std::string("{\"ev\":\"ui_preset_err\",\"reason\":\"bad_text\"}\n"));
        CHECK(f.st.saves == 0);
    }
    // ...and exactly T = 163 is ACCEPTED (the bound is inclusive) — and its record carries all 163 bytes (W6; was 17).
    const std::string full(mrnv::kUiPresetTextMax, 'k');
    f.zero();
    CHECK(f.run(("preset set dm1 loc=on \"" + full + "\"").c_str()));
    CHECK(std::strstr(f.out.buf, ("\"text\":\"" + full + "\"").c_str()) != nullptr);
    CHECK(f.cat.slot(mrfw::kPresetDmFirst).len == mrnv::kUiPresetTextMax);

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
        // ⓘ W6 (D14): `preset list all` LEFT this list — a token after `list` is a PAGE token now, and a bad one answers
        //   `bad_page` (NDJSON), not the usage line: see `w6-bad_page`.
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
    // ⓘ W6: the WIDEST record is `emergency` with a full 163-byte text and location off (243 B); pages and `bad_page`
    //   join (was `channel8` with 17 bytes).
    const std::string widest = "preset set emergency loc=off \"" + std::string(mrnv::kUiPresetTextMax, 'w') + "\"";
    const char* lines[] = {
        "preset list",
        "preset list 5",
        "preset list 9",                                      // a `bad_page` record
        widest.c_str(),                                       // the WIDEST record this file can produce
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

// ======================================================================== (10) THE FIVE BOOT-LINE STATES
// ⓘ W6 (D8): `old_v1` is the fifth arm — its own line, zero writes (was "the four storage states").
TEST_CASE("P2 the boot restore: the five storage states each drive the ruled line (or none), with zero writes") {
    struct { mrnv::UiPresetRead st; const char* expect; } arms[] = {
        { mrnv::UiPresetRead::ok,        nullptr },                        // ★ loaded — say NOTHING
        { mrnv::UiPresetRead::absent,    nullptr },                        // ★ a first boot is SILENT
        { mrnv::UiPresetRead::invalid,   mrfw::kPresetInvalidLine  },
        { mrnv::UiPresetRead::io_failed, mrfw::kPresetIoFailedLine },
        { mrnv::UiPresetRead::old_v1,    mrfw::kPresetOldV1Line    },      // ★ W6: at EVERY boot until replaced
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
    // ★ W6 (D8): an `old_v1` boot says so at EVERY boot — the store is untouched, so a second restore prints it again —
    //   and the first successful change REPLACES the record (even restating a default) and retires the line.
    FakePresetStore st4; FakeGate g4; mrfw::PresetDiag diag4; Rec out4;
    st4.state = mrnv::UiPresetRead::old_v1;
    for (int boot = 0; boot < 2; ++boot) {
        mrfw::PresetCatalog c{st4, g4}; out4.reset();
        CHECK(mrfw::preset_boot_restore(c, diag4, out4) == mrnv::UiPresetRead::old_v1);
        CHECK(std::strcmp(out4.buf, (std::string(mrfw::kPresetOldV1Line) + "\n").c_str()) == 0);
        CHECK(st4.saves == 0);
    }
    mrfw::PresetCatalog cat4{st4, g4};
    mrfw::preset_boot_restore(cat4, diag4, out4);
    CHECK(mrfw::preset_verb(cat4, diag4, "preset set dm1 loc=off \"Are you OK?\"", 36, out4));   // a restated default
    CHECK(st4.saves == 1);
    CHECK(st4.rec.version == mrnv::kUiPresetVersion);
    CHECK(diag4.line() == nullptr);
    // ★ The boot path's buffer is its OWN derived bound — the widest of the three lines (`invalid`'s, whose em dash is
    //   three UTF-8 bytes) + '\n' + NUL — and every line fits it whole.
    CHECK(mrfw::kPresetBootLineMax == 81);
    CHECK(std::strlen(mrfw::kPresetInvalidLine) + 2 == mrfw::kPresetBootLineMax);
    CHECK(std::strlen(mrfw::kPresetOldV1Line) + 2 <= mrfw::kPresetBootLineMax);
    CHECK(std::strlen(mrfw::kPresetIoFailedLine) + 2 <= mrfw::kPresetBootLineMax);
}

// ======================================================================== THE RESIDENT COST (spec §5)
TEST_CASE("P2 the resident cost of the ONE live catalog is measured, not assumed") {
    // ★ P2 is where the ruled no-stack placement is PAID: `preset_catalog()` holds one `PresetCatalog` in `.bss`.
    //   The figure is asserted so a future member cannot grow it silently; the per-board RAM delta is QG's.
    CHECK(sizeof(mrnv::UiPresetBlob) == 2852);           // W6 re-sync (D15), was 372
    CHECK(sizeof(mrfw::PresetCatalog) >= 3 * sizeof(mrnv::UiPresetBlob));
    CHECK(sizeof(mrfw::PresetCatalog) <= 3 * sizeof(mrnv::UiPresetBlob) + 32);
    // ⓘ The line buffer is a STACK local of the emitters and is bounded by the widest record — W6: DERIVED, 243 B +
    //   NUL = 244 (was the literal 160 over a 98-B widest record). Still a formatter-sized local in the 8 KB
    //   console/BLE task, and ⛔ nowhere near a catalog record on any stack.
    CHECK(mrfw::kPresetLineMax == 244);
    char b[mrfw::kPresetLineMax];
    mrnv::UiPresetBlob d{}; mrfw::preset_defaults(d);
    const std::string t(mrnv::kUiPresetTextMax, 'e');
    mrfw::preset_slot_put(d.slot[mrfw::kPresetEmergency], true, false, t.c_str(), t.size());   // enabled, 163, loc off
    const size_t widest = mrfw::write_ui_preset(b, sizeof b, mrfw::kPresetEmergency, d.slot[mrfw::kPresetEmergency]);
    CHECK(widest == 243);                                // ★ the real writer's maximum — the bound is EXACT, not slack
    CHECK(widest + 1 == mrfw::kPresetLineMax);
    // ...and ONE byte less loses the whole record (JsonBuf's latch answers 0) — the derivation is what keeps it.
    char short_b[mrfw::kPresetLineMax - 1];
    CHECK(mrfw::write_ui_preset(short_b, sizeof short_b, mrfw::kPresetEmergency, d.slot[mrfw::kPresetEmergency]) == 0);
    // the widest END record (a page, ten-digit generation) fits too
    CHECK(mrfw::write_ui_presets_end(b, sizeof b, 8, 8, 4294967295u, 5) > 0);
}
