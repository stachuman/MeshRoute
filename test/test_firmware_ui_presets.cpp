// MeshRoute — test/test_firmware_ui_presets.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-10/UI-11 slice P1 — the `/mrui` preset catalog (`src/firmware_ui_presets.h` + the record in `src/device_nv.h`).
// Spec: docs/superpowers/specs/2026-08-25-ui10-11-preset-catalog-spec.md §2 + §3-P1 (owner-approved 2026-08-25,
// QA round 2 folded in), over the parent design 2026-07-31-onboard-oled-ui-design.md §3.2.2 / §3.2.3.
//
// ★★★ WHY EVERY CASE COUNTS WRITES INSTEAD OF ASSERTING A VERDICT — the `/mrjoin` and `/mrteams` suites' rule, and it
//     is the same rule here: the verdict is a value the implementation CHOOSES, so a wrong implementation can choose
//     the right value while doing the wrong thing. A WRITE COUNT is a CONSEQUENCE. ⇒ "an identical set writes
//     nothing" is measured as `saves == 0`, and "an `io_failed` store refuses every mutation" is measured as the
//     stored record being BYTE-IDENTICAL afterwards — ⛔ never as "it returned `store`".
//
// ⛔ THE LIMIT OF THE CLAIM, unchanged from those two suites: the store is a FAKE. No NVS/LittleFS write, no flash
//    WEAR, no reset-during-write ([[B193]]). The `/mrui` power-cut behaviour is METAL-ONLY (M2).
#include <doctest.h>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include "firmware_ui_presets.h"
#include "firmware_ui_model.h"   // ★★ WAS the DRIFT FENCE's other side (`mrui::kDmTexts` / `kChannelTexts` /
                                 //    `kEmergencyText`). §UI-10/11 P3 RETIRED those tables, so the duplication this
                                 //    file fenced no longer exists — and the include stays, now for `mrui::
                                 //    compose_project`, which is what the panel actually renders the defaults
                                 //    THROUGH (see the case below).

namespace {

using mrfw::PresetVerdict;
using mrfw::PresetErr;
using mrfw::PresetKind;

// The COUNTING store. ★ `state` is the FOUR-valued answer, so a case can put the fake in `absent`, `invalid` or
// `io_failed` without forging bytes; `rec` moves only on a successful save, so "the stored record did not move" is
// measurable. `attempted` keeps what the LAST save was HANDED even when the save fails — which is how the
// candidate-carries-the-next-generation rule is measured rather than inferred.
struct FakePresetStore : mrfw::IUiPresetStore {
    mrnv::UiPresetBlob rec{};
    mrnv::UiPresetBlob attempted{};
    mrnv::UiPresetRead state = mrnv::UiPresetRead::ok;
    int  loads = 0, saves = 0;
    bool save_ok = true;
    // ★ On a NON-ok read the fake deposits GARBAGE, deliberately: the real `read_slot` may leave a PARTIAL record
    //   behind, and the service is required to re-init rather than trust it (device_nv.h's §nv-ritual warning).
    mrnv::UiPresetRead load(mrnv::UiPresetBlob& out) override {
        ++loads;
        if (state != mrnv::UiPresetRead::ok) { std::memset(&out, 0xA5, sizeof out); return state; }
        out = rec;
        return mrnv::UiPresetRead::ok;
    }
    bool save(const mrnv::UiPresetBlob& b) override {
        ++saves;
        attempted = b;
        if (!save_ok) return false;
        rec = b; state = mrnv::UiPresetRead::ok;
        return true;
    }
};

struct FakeGate : mrfw::IEmergencyGate {
    bool active = false;
    bool emergency_active() const override { return active; }
};

struct Fix {
    FakePresetStore store;
    FakeGate        gate;
    mrfw::PresetCatalog cat{store, gate};
    // ★ THE `/mrcfg` WITNESS (spec pin 7: *"`/mrcfg` untouched in every one"*). ⓘ Its strength is stated honestly:
    //   the service holds ONE seam and cannot reach a config record at all, so this is a STRUCTURAL pin that no
    //   mutation of the service header can redden. The ATTACKABLE half of the same rule lives in `device_nv.h`
    //   (`kSlotUi` / `kUiPresetMagic` vs `/mrcfg`'s) and is attacked there, by `--target=devicenv`.
    mrnv::Blob cfg_before{};
    Fix() { std::memset(&cfg_before, 0x5A, sizeof cfg_before); }
};

// A VALID stored record: the compiled defaults, canonically composed. The ordinary starting point for the `ok` state.
void seed_valid(FakePresetStore& s) {
    mrfw::preset_defaults(s.rec);
    s.state = mrnv::UiPresetRead::ok;
}
uint8_t slen(const char* s) { return static_cast<uint8_t>(std::strlen(s)); }
// `set` with a C string — every case in this file edits by phrase, not by length arithmetic.
mrfw::PresetResult set_text(mrfw::PresetCatalog& c, long slot, bool loc, const char* t) {
    return c.set(slot, loc, t, slen(t));
}
bool all_zero(const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) if (b[i]) return false;
    return true;
}

}  // namespace

// ==================================================================== THE RECORD AND ITS STORAGE-LEVEL FOUR STATES
TEST_CASE("ui10-p1-abi: the /mrui record's ABI is what the size check guards, and the tail padding is NAMED") {
    // The two per-ABI pins the header asserts, re-stated as runtime facts so a failure NAMES the number.
    CHECK(sizeof(mrnv::UiPresetSlot) == 21);
    CHECK(alignof(mrnv::UiPresetSlot) == 1);
    CHECK(sizeof(mrnv::UiPresetBlob) == 372);
    CHECK(mrnv::kUiPresets == 17);
    CHECK(mrnv::kUiPresetTextMax == 17);          // ★ OQ-A's owner ruling, and `text[18]` = 17 + the terminator
    CHECK(sizeof(mrnv::UiPresetSlot::text) == 18);

    // ★★ EVERY BYTE IS ACCOUNTED FOR BY A DECLARED MEMBER. If `reserved_tail` were dropped the struct would carry 3
    //    bytes of IMPLICIT padding — indeterminate after value-initialisation — and the whole-record compare that IS
    //    the write-coalescing policy would answer differently on identical catalogs.
    CHECK(offsetof(mrnv::UiPresetBlob, magic)      == 0);
    CHECK(offsetof(mrnv::UiPresetBlob, version)    == 4);
    CHECK(offsetof(mrnv::UiPresetBlob, reserved)   == 6);
    CHECK(offsetof(mrnv::UiPresetBlob, generation) == 8);
    CHECK(offsetof(mrnv::UiPresetBlob, slot)       == 12);
    CHECK(offsetof(mrnv::UiPresetBlob, reserved_tail) == 12 + 17 * 21);
    CHECK(sizeof(mrnv::UiPresetBlob) % alignof(mrnv::UiPresetBlob) == 0);

    // ★★★ ITS OWN MAGIC AND ITS OWN SLOT — the separation the design demands in as many words (*"editing a phrase
    //     must never reset radio, identity, team or key configuration"*). ⛔ A `/mrui` that carried `/mrcfg`'s magic
    //     or addressed `/mrcfg`'s slot would make a phrase edit reprovision the node.
    CHECK(mrnv::kUiPresetMagic == 0x4D525531u);   // 'MRU1'
    CHECK(mrnv::kUiPresetMagic != mrnv::kMagic);
    CHECK(mrnv::kUiPresetMagic != mrnv::kJoinMagic);
    CHECK(mrnv::kUiPresetMagic != mrnv::kTeamKeyMagic);
    CHECK(mrnv::kUiPresetMagic != mrnv::kIdMagic);
    CHECK(mrnv::kUiPresetMagic != mrnv::kPeersMagic);
    CHECK(std::strcmp(mrnv::kSlotUi.path, "/mrui") == 0);
    CHECK(std::strcmp(mrnv::kSlotUi.path, mrnv::kSlotCfg.path) != 0);
    CHECK(std::strcmp(mrnv::kSlotUi.key,  mrnv::kSlotCfg.key)  != 0);
    CHECK(std::strcmp(mrnv::kSlotUi.path, mrnv::kSlotJoin.path) != 0);
    CHECK(std::strcmp(mrnv::kSlotUi.path, mrnv::kSlotTeams.path) != 0);
    // ★ FACTORY RESET ERASES `/mrui`, and the ruling is DATA: the `"mr"` namespace is what `factory_erase()` clears
    //   in one shot. ⛔ `/mrfault`'s own namespace is the deliberate exception and a phrase store is not one.
    CHECK(std::strcmp(mrnv::kSlotUi.ns, "mr") == 0);
    CHECK(std::strcmp(mrnv::kSlotUi.ns, mrnv::kSlotFault.ns) != 0);
}

TEST_CASE("ui10-p1-abi: the storage-level four-state read keeps absent, invalid and io_failed apart") {
    mrnv::UiPresetBlob b{};
    mrnv::ui_preset_blob_init(b);
    const int n = static_cast<int>(sizeof b);

    CHECK(mrnv::ui_preset_blob_state(b, n) == mrnv::UiPresetRead::ok);
    CHECK(mrnv::ui_preset_blob_state(b, mrnv::kSlotAbsent) == mrnv::UiPresetRead::absent);
    // ★ THE BACKEND ARM IS ASKED FIRST, and it must be: a store that would not open returns `kSlotAbsent`, so any
    //   later ordering would launder a dead store into "no catalog configured".
    mrnv::SlotIo io{};
    io.backend_failed = true;
    CHECK(mrnv::ui_preset_blob_state(b, mrnv::kSlotAbsent, io) == mrnv::UiPresetRead::io_failed);
    mrnv::SlotIo over{};
    over.oversize = true;
    CHECK(mrnv::ui_preset_blob_state(b, n, over) == mrnv::UiPresetRead::invalid);   // ⛔ a valid PREFIX is not ok
    // Length, magic and version are EQUALITY policy — each rejects on its own.
    CHECK(mrnv::ui_preset_blob_state(b, n - 1) == mrnv::UiPresetRead::invalid);
    CHECK(mrnv::ui_preset_blob_state(b, n + 1) == mrnv::UiPresetRead::invalid);
    mrnv::UiPresetBlob wrong = b; wrong.magic = mrnv::kMagic;
    CHECK(mrnv::ui_preset_blob_state(wrong, n) == mrnv::UiPresetRead::invalid);
    wrong = b; wrong.version = mrnv::kUiPresetVersion + 1;
    CHECK(mrnv::ui_preset_blob_state(wrong, n) == mrnv::UiPresetRead::invalid);
    // ★ `ui_preset_blob_init` stamps the FIRST generation and it is 1, ⛔ never 0.
    CHECK(b.generation == 1);
    CHECK(b.reserved == 0);
    CHECK(all_zero(b.reserved_tail, sizeof b.reserved_tail));
}

// ===================================================================================== THE COMPILED DEFAULTS
TEST_CASE("ui10-p1-defaults: the compiled catalog is §3.2.2's table verbatim") {
    mrnv::UiPresetBlob b{};
    mrfw::preset_defaults(b);
    CHECK(mrfw::presets_canonical(b));
    CHECK(b.magic == mrnv::kUiPresetMagic);
    CHECK(b.version == mrnv::kUiPresetVersion);
    CHECK(b.generation == 1);

    // row 1 — emergency: enabled, MANDATORY, location ON
    CHECK(b.slot[0].enabled == 1);
    CHECK(b.slot[0].loc == 1);
    CHECK(std::strcmp(b.slot[0].text, "I'm in danger") == 0);
    // rows 2-3 — dm1/dm2 enabled, location OFF
    CHECK(b.slot[1].enabled == 1);  CHECK(b.slot[1].loc == 0);  CHECK(std::strcmp(b.slot[1].text, "Are you OK?") == 0);
    CHECK(b.slot[2].enabled == 1);  CHECK(b.slot[2].loc == 0);  CHECK(std::strcmp(b.slot[2].text, "I'm OK") == 0);
    // rows 4-5 — channel1/channel2 enabled, location OFF
    CHECK(b.slot[9].enabled  == 1); CHECK(b.slot[9].loc  == 0); CHECK(std::strcmp(b.slot[9].text, "Got your message") == 0);
    CHECK(b.slot[10].enabled == 1); CHECK(b.slot[10].loc == 0); CHECK(std::strcmp(b.slot[10].text, "All good") == 0);
    // ★ dm3..dm8 and channel3..channel8 — DISABLED and ALL-ZERO, which is the canonical form of "empty"
    for (uint8_t i : {3, 4, 5, 6, 7, 8, 11, 12, 13, 14, 15, 16}) {
        CHECK(b.slot[i].enabled == 0);
        CHECK(b.slot[i].loc == 0);
        CHECK(b.slot[i].len == 0);
        CHECK(all_zero(b.slot[i].text, sizeof b.slot[i].text));
    }
}

TEST_CASE("ui10-p1-defaults: THE DRIFT FENCE IS DISCHARGED — the compiled defaults ARE what the panel projects") {
    // ★★★★ **THE FENCE'S JOB IS OVER, AND THE WITHDRAWN CASE IS KEPT VISIBLE** (the correction idiom). It read:
    //         CHECK(std::strcmp(mrfw::kPresetDefaults[0].text, mrui::kEmergencyText) == 0);
    //         CHECK(std::strcmp(mrfw::kPresetDefaults[1].text, mrui::kDmTexts[0]) == 0);      … and three more
    //       — the five spellings asserted against a SECOND live copy of themselves, because P1 could not touch the
    //       model's fixed tables inside a storage slice (C1). §UI-10/11 P3 RETIRED those tables, so there is no
    //       second copy left to drift, and asserting one against itself would be a check that cannot fail.
    // ⇒ WHAT REPLACES IT IS THE PROPERTY THE FENCE WAS PROXYING FOR: the words the PANEL shows on an unconfigured
    //   device are the compiled defaults, arriving through the SHIPPED projection (`mrui::compose_project`) rather
    //   than through a table. That is a value relation over the real path, not a spelling compared to itself.
    Fix f;
    mrui::ComposeList dm{}, ch{};
    mrui::compose_project(f.cat.live(), PresetKind::dm,      dm);
    mrui::compose_project(f.cat.live(), PresetKind::channel, ch);
    CHECK(dm.n == 2);
    CHECK(ch.n == 2);
    CHECK(std::strcmp(dm.row[0].text, mrfw::kPresetDefaults[1].text)  == 0);
    CHECK(std::strcmp(dm.row[1].text, mrfw::kPresetDefaults[2].text)  == 0);
    CHECK(std::strcmp(ch.row[0].text, mrfw::kPresetDefaults[9].text)  == 0);
    CHECK(std::strcmp(ch.row[1].text, mrfw::kPresetDefaults[10].text) == 0);
    // ★ ...and each row carries its STABLE SLOT, which is the identity the retired tables could not express at all.
    CHECK(dm.row[0].slot == mrfw::kPresetDmFirst);
    CHECK(dm.row[1].slot == uint8_t(mrfw::kPresetDmFirst + 1));
    CHECK(ch.row[0].slot == mrfw::kPresetChannelFirst);
    CHECK(ch.row[1].slot == uint8_t(mrfw::kPresetChannelFirst + 1));
    // ★ THE EMERGENCY PHRASE IS NEVER A COMPOSE ROW (§3.2.2) — it reaches the wire only through the long press.
    for (uint8_t i = 0; i < dm.n; ++i) CHECK(dm.row[i].slot != mrfw::kPresetEmergency);
    for (uint8_t i = 0; i < ch.n; ++i) CHECK(ch.row[i].slot != mrfw::kPresetEmergency);
    CHECK(std::strcmp(f.cat.slot(mrfw::kPresetEmergency).text, mrfw::kPresetDefaults[0].text) == 0);
    // ★ AND THE COUNTS AGREE with the projection's own lengths.
    CHECK(f.cat.enabled_count(PresetKind::dm)      == dm.n);
    CHECK(f.cat.enabled_count(PresetKind::channel) == ch.n);
    // ⛔ emergency is NEVER a compose row (§3.2.2) and is counted into neither list.
    CHECK(f.cat.enabled_count(PresetKind::emergency) == 1);
    // ★ EVERY compiled phrase satisfies the validator that guards a `set` — a default the wearer could not re-enter
    //   would be a catalog with two different text policies.
    for (uint8_t i = 0; i < mrnv::kUiPresets; ++i)
        if (mrfw::kPresetDefaults[i].text)
            CHECK(mrfw::validate_preset_text(mrfw::kPresetDefaults[i].text,
                                             slen(mrfw::kPresetDefaults[i].text)) == PresetErr::none);
}

// ============================================================================ PIN 7 — THE FOUR STATES EACH BEHAVE
TEST_CASE("ui10-p1-states: ABSENT is an ordinary first boot — defaults, ⛔ NO warning, ⛔ no line, zero writes") {
    Fix f;
    f.store.state = mrnv::UiPresetRead::absent;
    const mrnv::UiPresetRead st = f.cat.begin();
    CHECK(st == mrnv::UiPresetRead::absent);
    CHECK(f.store.saves == 0);                       // ⛔ a read NEVER writes, on any path
    CHECK(f.cat.invalid_loads() == 0);
    CHECK(f.cat.io_failed_loads() == 0);
    CHECK(mrfw::preset_boot_line(st) == nullptr);    // ★ SILENT — a first boot is not a fault
    CHECK(f.cat.generation() == 1);
    CHECK(std::strcmp(f.cat.slot(0).text, "I'm in danger") == 0);
    CHECK(mrfw::presets_canonical(f.cat.live()));
    CHECK(std::memcmp(&f.cfg_before, &f.cfg_before, sizeof f.cfg_before) == 0);   // pin 7: /mrcfg untouched
}

TEST_CASE("ui10-p1-states: INVALID runs the defaults and WARNS — counted, worded, and repairable") {
    Fix f;
    f.store.state = mrnv::UiPresetRead::invalid;
    const mrnv::UiPresetRead st = f.cat.begin();
    CHECK(st == mrnv::UiPresetRead::invalid);
    CHECK(f.store.saves == 0);                       // ⛔ NOT repaired on the read
    CHECK(f.cat.invalid_loads() == 1);               // ★ COUNTED (spec §3-P1)
    CHECK(f.cat.io_failed_loads() == 0);             // ★ and DISTINCT from the other warning
    CHECK(mrfw::preset_boot_line(st) != nullptr);
    CHECK(std::strcmp(mrfw::preset_boot_line(st),
                      "  ui presets = DEFAULTS (record invalid — repaired on next successful change)") == 0);
    // ★ the panel still shows a COMPLETE, CANONICAL catalog — the wearer is never left with garbage on screen
    CHECK(mrfw::presets_canonical(f.cat.live()));
    CHECK(std::strcmp(f.cat.slot(1).text, "Are you OK?") == 0);
}

TEST_CASE("ui10-p1-states: IO_FAILED runs the defaults and warns DISTINCTLY") {
    Fix f;
    f.store.state = mrnv::UiPresetRead::io_failed;
    const mrnv::UiPresetRead st = f.cat.begin();
    CHECK(st == mrnv::UiPresetRead::io_failed);
    CHECK(f.store.saves == 0);
    CHECK(f.cat.io_failed_loads() == 1);
    CHECK(f.cat.invalid_loads() == 0);               // ⛔ ⛔ NOT the same warning as a corrupt record
    CHECK(std::strcmp(mrfw::preset_boot_line(st),
                      "  ui presets = DEFAULTS (store unreadable — changes disabled)") == 0);
    CHECK(mrfw::presets_canonical(f.cat.live()));
}

TEST_CASE("ui10-p1-states: VALID is LOADED — the wearer's configured phrases, not the compiled ones") {
    Fix f;
    seed_valid(f.store);
    mrfw::preset_slot_put(f.store.rec.slot[3], true, true, "meet at the col", slen("meet at the col"));
    f.store.rec.generation = 44;
    const mrnv::UiPresetRead st = f.cat.begin();
    CHECK(st == mrnv::UiPresetRead::ok);
    CHECK(mrfw::preset_boot_line(st) == nullptr);    // ★ a valid store prints NO presets line
    CHECK(f.cat.generation() == 44);
    CHECK(f.cat.slot(3).enabled == 1);
    CHECK(f.cat.slot(3).loc == 1);
    CHECK(std::strcmp(f.cat.slot(3).text, "meet at the col") == 0);
    CHECK(f.cat.enabled_count(PresetKind::dm) == 3);
    CHECK(f.store.saves == 0);
    CHECK(f.cat.invalid_loads() == 0);
    CHECK(f.cat.io_failed_loads() == 0);
}

TEST_CASE("ui10-p1-states: the two boot lines are the ONLY two, and `ok`/`absent` are silent") {
    // The whole four-valued domain swept, so an arm added to `UiPresetRead` without a wording decision is visible.
    CHECK(mrfw::preset_boot_line(mrnv::UiPresetRead::ok)        == nullptr);
    CHECK(mrfw::preset_boot_line(mrnv::UiPresetRead::absent)    == nullptr);
    CHECK(mrfw::preset_boot_line(mrnv::UiPresetRead::invalid)   == mrfw::kPresetInvalidLine);
    CHECK(mrfw::preset_boot_line(mrnv::UiPresetRead::io_failed) == mrfw::kPresetIoFailedLine);
    CHECK(std::strcmp(mrfw::kPresetInvalidLine, mrfw::kPresetIoFailedLine) != 0);
}

// ============================================= QA ROUND 2 — `valid` REQUIRES FULL SEMANTIC VALIDATION, NOT A HEADER
TEST_CASE("ui10-p1-semantic: a size/header-valid record with ANY canonical violation classifies INVALID") {
    // ★★★★ EACH VIOLATION ON ITS OWN, from a record that is otherwise perfect — so no assertion below can pass for a
    //      second reason. A corrupted slot field must NEVER ride in as valid.
    struct Case { const char* what; void (*break_it)(mrnv::UiPresetBlob&); };
    const Case cases[] = {
        { "generation 0",              [](mrnv::UiPresetBlob& b) { b.generation = 0; } },
        { "header reserved dirty",     [](mrnv::UiPresetBlob& b) { b.reserved = 1; } },
        { "tail padding dirty",        [](mrnv::UiPresetBlob& b) { b.reserved_tail[2] = 1; } },
        { "enabled is not 0/1",        [](mrnv::UiPresetBlob& b) { b.slot[1].enabled = 7; } },
        { "loc is not 0/1",            [](mrnv::UiPresetBlob& b) { b.slot[1].loc = 2; } },
        { "len past the OQ-A bound",   [](mrnv::UiPresetBlob& b) { b.slot[1].len = 18; } },
        { "len past the buffer",       [](mrnv::UiPresetBlob& b) { b.slot[1].len = 40; } },
        { "emergency DISABLED",        [](mrnv::UiPresetBlob& b) { b.slot[0].enabled = 0; } },
        // ★★★★ THE ONE THAT ISOLATES THE MANDATORY RULE, and it was ADDED after the battery measured its absence:
        //      with only the row above, `enabled = 0` was caught by the DISABLED-slot rule (the phrase was still in
        //      the buffer), so dropping *"the emergency slot is ALWAYS enabled"* from the reader left the suite GREEN.
        //      A slot that is disabled AND canonically zeroed is a perfectly well-formed DM/channel slot — only the
        //      mandatory term can reject it at index 0. ⇒ this is the shape a `clear emergency` would leave behind if
        //      one could ever be written, and the reader must refuse to adopt it.
        { "emergency cleared AND zeroed", [](mrnv::UiPresetBlob& b) { b.slot[0] = mrnv::UiPresetSlot{}; } },
        { "emergency EMPTY",           [](mrnv::UiPresetBlob& b) { b.slot[0].len = 0;
                                                                   std::memset(b.slot[0].text, 0, 18); } },
        { "enabled slot, empty text",  [](mrnv::UiPresetBlob& b) { b.slot[1].len = 0; } },
        { "enabled slot, all spaces",  [](mrnv::UiPresetBlob& b) { std::memset(b.slot[1].text, ' ', 3);
                                                                   b.slot[1].len = 3;
                                                                   std::memset(b.slot[1].text + 3, 0, 15); } },
        { "enabled slot, control byte",[](mrnv::UiPresetBlob& b) { b.slot[1].text[2] = '\n'; } },
        { "enabled slot, quote byte",  [](mrnv::UiPresetBlob& b) { b.slot[1].text[2] = '"'; } },
        { "enabled slot, backslash",   [](mrnv::UiPresetBlob& b) { b.slot[1].text[2] = '\\'; } },
        { "enabled slot, high byte",   [](mrnv::UiPresetBlob& b) { b.slot[1].text[2] = char(0xC3); } },
        { "GARBAGE AFTER len",         [](mrnv::UiPresetBlob& b) { b.slot[1].text[17] = 'x'; } },
        { "disabled slot keeps text",  [](mrnv::UiPresetBlob& b) { b.slot[5].text[0] = 'x'; } },
        { "disabled slot keeps len",   [](mrnv::UiPresetBlob& b) { b.slot[5].len = 3; } },
        { "disabled slot keeps loc",   [](mrnv::UiPresetBlob& b) { b.slot[5].loc = 1; } },
    };
    for (const Case& c : cases) {
        CAPTURE(c.what);
        mrnv::UiPresetBlob b{};
        mrfw::preset_defaults(b);
        CHECK(mrfw::presets_canonical(b));          // the control: it was valid until this case broke it
        c.break_it(b);
        CHECK_FALSE(mrfw::presets_canonical(b));

        // ★★ AND THE SERVICE MUST AGREE, end to end: the storage layer says `ok` for every one of these (the length,
        //    magic and version are untouched), so ONLY the semantic gate can catch them.
        Fix f;
        f.store.rec = b;
        f.store.state = mrnv::UiPresetRead::ok;
        CHECK(mrnv::ui_preset_blob_state(b, int(sizeof b)) == mrnv::UiPresetRead::ok);
        CHECK(f.cat.begin() == mrnv::UiPresetRead::invalid);
        CHECK(f.cat.invalid_loads() == 1);
        CHECK(f.store.saves == 0);                    // ⛔ still not repaired on a read
        CHECK(mrfw::presets_canonical(f.cat.live())); // the wearer gets a complete canonical catalog regardless
    }
}

// ================================================================== PIN 12 — THE CANONICAL BYTES THE WRITER EMITS
TEST_CASE("ui10-p1-canonical: the written record obeys every canonical-byte rule") {
    Fix f;
    seed_valid(f.store);
    f.cat.begin();
    // A LONG phrase first, then a SHORT one into the same slot: the tail of the long text must not survive.
    CHECK(set_text(f.cat, 4, true, "seventeen chars!!").verdict == PresetVerdict::ok);
    CHECK(f.store.rec.slot[4].len == 17);
    CHECK(set_text(f.cat, 4, false, "hi").verdict == PresetVerdict::ok);

    const mrnv::UiPresetSlot& s = f.store.rec.slot[4];
    CHECK(s.enabled == 1);                       // ★ exactly 1, ⛔ never "some non-zero"
    CHECK(s.loc == 0);                           // ★ exactly 0
    CHECK(s.len == 2);
    CHECK(std::strcmp(s.text, "hi") == 0);
    // ★★ EVERY BYTE AT AND AFTER `len` IS ZERO — the rule that makes the whole-record compare trustworthy.
    for (uint8_t i = s.len; i < sizeof s.text; ++i) CHECK(s.text[i] == 0);
    // ★ a DISABLED slot is all zero: length, text AND location
    CHECK(f.cat.clear(4).verdict == PresetVerdict::ok);
    CHECK(f.store.rec.slot[4].enabled == 0);
    CHECK(f.store.rec.slot[4].loc == 0);
    CHECK(f.store.rec.slot[4].len == 0);
    CHECK(all_zero(f.store.rec.slot[4].text, sizeof f.store.rec.slot[4].text));
    // ★ the emergency slot carries `enabled = 1` in EVERY record this service writes
    CHECK(f.store.rec.slot[0].enabled == 1);
    // ★ the NAMED padding is zero in every written record
    CHECK(f.store.rec.reserved == 0);
    CHECK(all_zero(f.store.rec.reserved_tail, sizeof f.store.rec.reserved_tail));
    // ★ and the whole thing is canonical by its own predicate — the writer and the reader agree
    CHECK(mrfw::presets_canonical(f.store.rec));
}

TEST_CASE("ui10-p1-canonical: the composition path zeroes the slot WHOLE, so nothing survives an edit") {
    mrnv::UiPresetSlot s{};
    std::memset(&s, 0xEE, sizeof s);
    mrfw::preset_slot_put(s, true, true, "abc", 3);
    CHECK(s.enabled == 1);
    CHECK(s.loc == 1);
    CHECK(s.len == 3);
    for (uint8_t i = 3; i < sizeof s.text; ++i) CHECK(s.text[i] == 0);
    // DISABLED ⇒ the zeroed slot, by construction — location and text are dropped with it
    std::memset(&s, 0xEE, sizeof s);
    mrfw::preset_slot_put(s, false, true, "abc", 3);
    CHECK(all_zero(&s, sizeof s));
}

// ==================================================================================== PIN 12 — THE GENERATION
TEST_CASE("ui10-p1-generation: it starts at 1, moves ONLY on a durable change, and SKIPS ZERO on wrap") {
    CHECK(mrfw::preset_generation_next(1) == 2);
    CHECK(mrfw::preset_generation_next(41) == 42);
    // ★★★ THE WRAP. `0` is reserved for "no generation", so a wrap must land on 1 — a zero would disarm P3's
    //     stale-generation refusal, which compares the generation a `SendReq` sealed.
    CHECK(mrfw::preset_generation_next(0xFFFFFFFFu) == 1);
    CHECK(mrfw::preset_generation_next(0xFFFFFFFFu) != 0);

    Fix f;
    seed_valid(f.store);
    f.store.rec.generation = 0xFFFFFFFFu;
    f.cat.begin();
    CHECK(f.cat.generation() == 0xFFFFFFFFu);
    CHECK(set_text(f.cat, 5, false, "wrap").verdict == PresetVerdict::ok);
    CHECK(f.cat.generation() == 1);                    // ★ end to end: the wrap landed on 1, ⛔ not on 0
    CHECK(f.store.rec.generation == 1);

    // ⛔ AND IT DOES NOT MOVE ON THE THREE NON-DURABLE OUTCOMES.
    const uint32_t g = f.cat.generation();
    CHECK(set_text(f.cat, 5, false, "wrap").verdict == PresetVerdict::unchanged);   // identical no-op
    CHECK(f.cat.generation() == g);
    CHECK(set_text(f.cat, 99, false, "x").err == PresetErr::bad_slot);              // refusal
    CHECK(f.cat.generation() == g);
    f.store.save_ok = false;
    CHECK(set_text(f.cat, 5, false, "different").verdict == PresetVerdict::nv_failed);
    CHECK(f.cat.generation() == g);                                                 // ⛔ a failed save publishes NOTHING
}

// ================================================== PIN 8 — COALESCING, WITH THE OWNER'S RULED LIMIT ON IT
TEST_CASE("ui10-p1-coalescing: an identical set writes NOTHING over a valid store") {
    Fix f;
    seed_valid(f.store);
    f.cat.begin();
    CHECK(f.store.saves == 0);
    // dm1's compiled value, re-stated exactly: same text, same location flag.
    const mrfw::PresetResult r = set_text(f.cat, 1, false, "Are you OK?");
    CHECK(r.verdict == PresetVerdict::unchanged);
    CHECK(r.err == PresetErr::none);
    CHECK(f.store.saves == 0);                 // ★ THE FLASH-WEAR GUARD, MEASURED
    CHECK(f.cat.generation() == 1);            // ⛔ and the generation did not move
    // A REAL change costs EXACTLY ONE write, and a second identical one costs zero again.
    CHECK(set_text(f.cat, 1, false, "Are you well?").verdict == PresetVerdict::ok);
    CHECK(f.store.saves == 1);
    CHECK(set_text(f.cat, 1, false, "Are you well?").verdict == PresetVerdict::unchanged);
    CHECK(f.store.saves == 1);
    // ★ THE LOCATION FLAG IS PART OF THE RECORD: same text, different `loc` is a CHANGE, ⛔ never a no-op.
    CHECK(set_text(f.cat, 1, true, "Are you well?").verdict == PresetVerdict::ok);
    CHECK(f.store.saves == 2);
    // `clear` of an ALREADY-disabled slot, and `reset` of an untouched one, are likewise zero-write successes.
    CHECK(f.cat.clear(7).verdict == PresetVerdict::unchanged);
    CHECK(f.cat.reset_slot(2).verdict == PresetVerdict::unchanged);
    CHECK(f.store.saves == 2);
    // ★★★ AND SO IS `reset all` OVER AN ALREADY-COMPILED CATALOG — **at a generation that has moved**, which is the
    //     case that keeps `reset_all`'s candidate carrying the LIVE generation rather than `preset_defaults`' 1. If it
    //     reset the counter, this second `reset all` would compare unequal and write, and — far worse — a `SendReq`
    //     sealed under an OLD catalog could compare EQUAL to the restored one, silently disarming P3's refusal.
    CHECK(f.cat.reset_all().verdict == PresetVerdict::ok);        // undo the two edits above
    const uint32_t g_after = f.cat.generation();
    CHECK(g_after > 1);
    CHECK(f.store.saves == 3);
    CHECK(f.cat.reset_all().verdict == PresetVerdict::unchanged);
    CHECK(f.store.saves == 3);                                    // ★ ZERO further writes
    CHECK(f.cat.generation() == g_after);                         // ⛔ and the generation did NOT go back to 1
}

TEST_CASE("ui10-p1-coalescing: it holds over an ABSENT store too — the owner's other in-scope arm") {
    Fix f;
    f.store.state = mrnv::UiPresetRead::absent;
    f.cat.begin();
    // Re-stating a compiled default over an absent store changes nothing that is running ⇒ ⛔ ZERO writes.
    CHECK(set_text(f.cat, 2, false, "I'm OK").verdict == PresetVerdict::unchanged);
    CHECK(f.store.saves == 0);
    CHECK(f.cat.generation() == 1);
    // A real change over an absent store costs EXACTLY ONE write (the record and its content land together).
    CHECK(set_text(f.cat, 2, false, "on my way").verdict == PresetVerdict::ok);
    CHECK(f.store.saves == 1);
    CHECK(f.cat.generation() == 2);
    CHECK(mrfw::presets_canonical(f.store.rec));
}

TEST_CASE("ui10-p1-coalescing: ⛔ THE PIN DOES NOT HOLD OVER `invalid` — a repair write is legitimate there") {
    // ★★★★ THE OWNER'S RULED LIMIT, and it is the sharp edge of the whole coalescing policy: over a CORRUPT record
    //      the compare has no meaning, so a mutation whose live values equal the defaults must STILL write — that
    //      write is the REPAIR. Reporting `unchanged` here would leave the record corrupt forever while telling the
    //      wearer his catalog was fine.
    Fix f;
    f.store.state = mrnv::UiPresetRead::invalid;
    f.cat.begin();
    CHECK(f.cat.invalid_loads() == 1);
    CHECK(f.store.saves == 0);
    // dm1's COMPILED value, re-stated — the exact input that is a zero-write no-op over a valid or absent store.
    const mrfw::PresetResult r = set_text(f.cat, 1, false, "Are you OK?");
    CHECK(r.verdict == PresetVerdict::ok);            // ⛔ NOT `unchanged`
    CHECK(f.store.saves == 1);                        // ★ ONE write: the repair
    CHECK(f.store.state == mrnv::UiPresetRead::ok);   // and the store is well again
    CHECK(mrfw::presets_canonical(f.store.rec));      // ★ THE COMPLETE CANONICAL CATALOG was rewritten
    CHECK(std::strcmp(f.store.rec.slot[0].text, "I'm in danger") == 0);
    CHECK(std::strcmp(f.store.rec.slot[9].text, "Got your message") == 0);
    CHECK(f.store.rec.generation == 2);
    // And a SECOND identical set — now over the repaired, valid record — coalesces again.
    CHECK(set_text(f.cat, 1, false, "Are you OK?").verdict == PresetVerdict::unchanged);
    CHECK(f.store.saves == 1);
}

// ======================================================================= PIN 3 — THE EMERGENCY SLOT'S INVARIANTS
TEST_CASE("ui10-p1-emergency: it can NEVER be cleared or disabled") {
    Fix f;
    seed_valid(f.store);
    f.cat.begin();
    const mrfw::PresetResult r = f.cat.clear(mrfw::kPresetEmergency);
    CHECK(r.verdict == PresetVerdict::refused);
    CHECK(r.err == PresetErr::mandatory);
    CHECK(f.store.loads == 1);                 // ⛔ ZERO loads for the refusal (the one load was `begin`)
    CHECK(f.store.saves == 0);                 // ⛔ ZERO writes
    CHECK(f.cat.slot(0).enabled == 1);
    CHECK(std::strcmp(f.cat.slot(0).text, "I'm in danger") == 0);
    // ⛔ AND IT CANNOT BE EMPTIED THROUGH `set` EITHER — the text validator refuses an empty or all-space phrase, so
    //    there is no second door to the same end.
    CHECK(f.cat.set(mrfw::kPresetEmergency, true, "", 0).err == PresetErr::bad_text);
    CHECK(set_text(f.cat, mrfw::kPresetEmergency, true, "   ").err == PresetErr::bad_text);
    CHECK(f.store.saves == 0);
    CHECK(f.cat.slot(0).enabled == 1);
}

TEST_CASE("ui10-p1-emergency: it IS text-editable, and every edit keeps it enabled") {
    Fix f;
    seed_valid(f.store);
    f.cat.begin();
    CHECK(set_text(f.cat, mrfw::kPresetEmergency, true, "HELP - leg broken").verdict == PresetVerdict::ok);
    CHECK(std::strcmp(f.cat.slot(0).text, "HELP - leg broken") == 0);
    CHECK(f.cat.slot(0).enabled == 1);
    CHECK(f.cat.slot(0).loc == 1);
    CHECK(mrfw::presets_canonical(f.store.rec));
    // ★ its LOCATION flag is editable too — §3.2.3's emergency row is a POLICY about a fix, not a locked flag
    CHECK(set_text(f.cat, mrfw::kPresetEmergency, false, "HELP - leg broken").verdict == PresetVerdict::ok);
    CHECK(f.cat.slot(0).loc == 0);
    CHECK(f.cat.slot(0).enabled == 1);
    // `reset emergency` restores the compiled phrase — a text edit, ⛔ not a clear, so it is ALLOWED
    CHECK(f.cat.reset_slot(mrfw::kPresetEmergency).verdict == PresetVerdict::ok);
    CHECK(std::strcmp(f.cat.slot(0).text, "I'm in danger") == 0);
    CHECK(f.cat.slot(0).loc == 1);
    CHECK(f.cat.slot(0).enabled == 1);
    // and `reset all` never disables it either
    CHECK(set_text(f.cat, 3, false, "x marks it").verdict == PresetVerdict::ok);
    CHECK(f.cat.reset_all().verdict == PresetVerdict::ok);
    CHECK(f.cat.slot(0).enabled == 1);
    CHECK(f.cat.slot(3).enabled == 0);
    CHECK(f.cat.enabled_count(PresetKind::dm) == 2);
}

// ================================================================ SPEC §2 — `busy`: AN ACTIVE EMERGENCY SERIES
TEST_CASE("ui10-p1-busy: an ACTIVE emergency refuses EVERY mutating verb, including a no-op") {
    Fix f;
    seed_valid(f.store);
    f.cat.begin();
    const int loads_after_begin = f.store.loads;
    f.gate.active = true;

    // ★ INCLUDING A NO-OP: `set` to the value already stored, which over a valid store would be `unchanged`.
    for (const mrfw::PresetResult r : { set_text(f.cat, 1, false, "Are you OK?"),
                                        set_text(f.cat, 1, false, "something new"),
                                        f.cat.clear(2),
                                        f.cat.reset_slot(2),
                                        f.cat.reset_all() }) {
        CHECK(r.verdict == PresetVerdict::refused);
        CHECK(r.err == PresetErr::busy);
    }
    CHECK(f.store.loads == loads_after_begin);   // ⛔ ZERO loads
    CHECK(f.store.saves == 0);                   // ⛔ ZERO writes
    CHECK(f.cat.generation() == 1);
    // ⛔ AND `busy` OUTRANKS EVERY OTHER REFUSAL: an alarm in flight is not the moment to explain a bad slot.
    CHECK(f.cat.clear(mrfw::kPresetEmergency).err == PresetErr::busy);
    CHECK(set_text(f.cat, 99, false, "x").err == PresetErr::busy);
    CHECK(f.cat.set(4, false, "", 0).err == PresetErr::busy);
    // The series ends -> the same verbs work again.
    f.gate.active = false;
    CHECK(set_text(f.cat, 1, false, "Are you OK?").verdict == PresetVerdict::unchanged);
    CHECK(set_text(f.cat, 1, false, "something new").verdict == PresetVerdict::ok);
    CHECK(f.store.saves == 1);
}

// ====================================== PIN 12 — THE CANDIDATE CARRIES THE GENERATION; THE PUBLISH IS AFTER THE SAVE
TEST_CASE("ui10-p1-order: the SAVED candidate already carries the next generation, and a failed save publishes NOTHING") {
    Fix f;
    seed_valid(f.store);
    f.cat.begin();
    CHECK(f.cat.generation() == 1);

    // ★★★ THE ORDER (spec §2, corrected QA round 2): construct the canonical candidate WITH the next non-zero
    //     generation -> save -> publish. The record the store was HANDED is the measurement: if the increment
    //     happened after the save, `attempted.generation` would still read 1.
    CHECK(set_text(f.cat, 6, false, "on the ridge").verdict == PresetVerdict::ok);
    CHECK(f.store.attempted.generation == 2);
    CHECK(f.store.rec.generation == 2);
    CHECK(f.cat.generation() == 2);

    // ★★★★ THE FAILED SAVE. ⛔ NOTHING is published: not the phrase, not the generation. Publishing first would make
    //      a node whose flash write failed show — and SEND — phrases that vanish at the next boot ([[B240]]'s shape).
    f.store.save_ok = false;
    const mrfw::PresetResult r = set_text(f.cat, 6, false, "in the corrie");
    CHECK(r.verdict == PresetVerdict::nv_failed);
    CHECK(r.err == PresetErr::store);
    CHECK(f.store.saves == 2);                                    // the ONE further attempt was made
    CHECK(f.store.attempted.generation == 3);                     // ★ the candidate carried it BEFORE the save
    CHECK(f.cat.generation() == 2);                               // ⛔ the live catalog did NOT move
    CHECK(std::strcmp(f.cat.slot(6).text, "on the ridge") == 0);     // ⛔ nor did the phrase
    CHECK(f.store.rec.generation == 2);                           // ⛔ nor did the store's last good record
    CHECK(std::strcmp(f.store.rec.slot[6].text, "on the ridge") == 0);
}

// ================================================= PIN 7's io_failed ARM — EVERY MUTATION, ZERO WRITES, NO REWRITE
TEST_CASE("ui10-p1-io: an io_failed store refuses EVERY mutation with `store` and writes NOTHING") {
    Fix f;
    seed_valid(f.store);
    f.cat.begin();
    CHECK(set_text(f.cat, 3, true, "the wearer's own").verdict == PresetVerdict::ok);
    const mrnv::UiPresetBlob intact = f.store.rec;   // ★ what a POSSIBLY-INTACT record looks like

    f.store.state = mrnv::UiPresetRead::io_failed;
    for (const mrfw::PresetResult r : { set_text(f.cat, 1, false, "anything"),
                                        set_text(f.cat, 1, false, "Are you OK?"),   // even a no-op
                                        f.cat.clear(3),
                                        f.cat.reset_slot(3),
                                        f.cat.reset_all() }) {
        CHECK(r.verdict == PresetVerdict::refused);
        CHECK(r.err == PresetErr::store);
    }
    CHECK(f.store.saves == 1);                                   // ⛔ NOT ONE further write
    CHECK(std::memcmp(&f.store.rec, &intact, sizeof intact) == 0);  // ★ the wearer's record is untouched
    CHECK(f.cat.io_failed_loads() == 5);                         // counted at every load, distinctly
    CHECK(f.cat.invalid_loads() == 0);
    // ⛔⛔ AND THE DISTINCTION FROM `invalid` IS THE POINT: the SAME sequence over a corrupt record REPAIRS it.
    f.store.state = mrnv::UiPresetRead::invalid;
    CHECK(set_text(f.cat, 1, false, "Are you OK?").verdict == PresetVerdict::ok);
    CHECK(f.store.saves == 2);
}

// ================================================================================ VALIDATION — OQ-A's BOUND AND §3.2.2
TEST_CASE("ui10-p1-validation: 1..17 printable ASCII, both sides of the bound, for BOTH location states") {
    const char* k17 = "seventeen chars!!";
    const char* k18 = "eighteen chars!!!!";
    CHECK(slen(k17) == 17);
    CHECK(slen(k18) == 18);
    CHECK(mrfw::validate_preset_text(k17, 17) == PresetErr::none);
    CHECK(mrfw::validate_preset_text(k18, 18) == PresetErr::bad_text);
    CHECK(mrfw::validate_preset_text("a", 1) == PresetErr::none);
    CHECK(mrfw::validate_preset_text("", 0) == PresetErr::bad_text);
    CHECK(mrfw::validate_preset_text(nullptr, 3) == PresetErr::bad_text);

    // ★★★ OQ-A: 17 FOR EVERY PRESET, IN BOTH LOCATION STATES. ⛔ The withdrawn draft's conditional bound (18 when
    //     `loc=off`) would accept an 18-byte phrase here — the row always shows a location marker, so it would be
    //     clipped and the wearer would send a suffix he could not inspect.
    Fix f;
    seed_valid(f.store);
    f.cat.begin();
    for (uint8_t s : {uint8_t(0), uint8_t(1), uint8_t(9)}) {
        for (bool loc : {false, true}) {
            CHECK(f.cat.set(s, loc, k17, 17).verdict != PresetVerdict::refused);
            CHECK(f.cat.set(s, loc, k18, 18).err == PresetErr::bad_text);
        }
    }

    // The forbidden bytes, each on its own.
    CHECK(mrfw::validate_preset_text("say \"hi\"", 8) == PresetErr::bad_text);   // `"` breaks the quoted forms
    CHECK(mrfw::validate_preset_text("a\\b", 3) == PresetErr::bad_text);         // `\`  likewise
    CHECK(mrfw::validate_preset_text("a\rb", 3) == PresetErr::bad_text);         // CR — excluded by the range
    CHECK(mrfw::validate_preset_text("a\nb", 3) == PresetErr::bad_text);         // LF — likewise
    { char t[3] = {'a', '\0', 'b'}; CHECK(mrfw::validate_preset_text(t, 3) == PresetErr::bad_text); }
    { char t[3] = {'a', char(0x7f), 'b'}; CHECK(mrfw::validate_preset_text(t, 3) == PresetErr::bad_text); }  // DEL
    { char t[3] = {'a', char(0xE9), 'b'}; CHECK(mrfw::validate_preset_text(t, 3) == PresetErr::bad_text); }  // high
    { char t[2] = {'a', char(0x1f)};      CHECK(mrfw::validate_preset_text(t, 2) == PresetErr::bad_text); }
    // ★ ≥ 1 NON-SPACE: a phrase of spaces renders as a row the wearer believes is configured and cannot see.
    CHECK(mrfw::validate_preset_text("   ", 3) == PresetErr::bad_text);
    CHECK(mrfw::validate_preset_text(" a ", 3) == PresetErr::none);
    // ★ THE PRINTABLE DOMAIN IS `mrui::ui_display_byte`'s, and this is the case that keeps them ONE policy.
    for (int c = 0; c < 256; ++c) {
        const char t[2] = { char(c), 'a' };
        const bool ok_here = mrfw::validate_preset_text(t, 2) == PresetErr::none;
        const bool shown   = mrui::ui_display_byte(uint8_t(c)) == char(c);
        if (c != '"' && c != '\\') CHECK(ok_here == shown);
        else CHECK_FALSE(ok_here);
    }
}

TEST_CASE("ui10-p1-validation: ★ A 273-BYTE PHRASE IS REFUSED — the length check is not bypassable by narrowing") {
    // ★★★★ THE REGRESSION, AND THE NUMBER IS THE DEFECT: with a `uint8_t len` boundary, 273 narrows AT THE CALL to
    //      273 & 0xFF = **17** — the exact bound — so a 273-byte phrase was ACCEPTED as a valid 17-byte one and
    //      `memcpy`'d as 17. The validator could not have caught it: it never saw the real length. ⇒ the boundary is
    //      `size_t` and the bound is tested on the WIDE value, before any narrowing.
    char big[300];
    std::memset(big, 'a', sizeof big);
    const size_t n273 = 273;                       // 273 & 0xFF == 17  — the collision, spelled out
    CHECK((n273 & 0xFFu) == mrnv::kUiPresetTextMax);
    CHECK(mrfw::validate_preset_text(big, n273) == PresetErr::bad_text);
    // The other two collisions in the same family, each on its own: 256 -> 0 ("empty"), 274 -> 18 ("too long").
    CHECK(mrfw::validate_preset_text(big, size_t(256)) == PresetErr::bad_text);
    CHECK(mrfw::validate_preset_text(big, size_t(274)) == PresetErr::bad_text);
    CHECK(mrfw::validate_preset_text(big, size_t(18))  == PresetErr::bad_text);
    CHECK(mrfw::validate_preset_text(big, size_t(17))  == PresetErr::none);   // the control: 17 still passes

    // ★★ AND THROUGH THE PUBLIC BOUNDARY, which is where the narrowing actually happened — ⛔ zero writes, and the
    //    slot is untouched, so nothing was `memcpy`'d under a laundered length either.
    Fix f;
    seed_valid(f.store);
    f.cat.begin();
    for (size_t n : { size_t(256), n273, size_t(274), size_t(4096) }) {
        const mrfw::PresetResult r = f.cat.set(1, false, big, n);
        CHECK(r.verdict == PresetVerdict::refused);
        CHECK(r.err == PresetErr::bad_text);
    }
    CHECK(f.store.saves == 0);
    CHECK(std::strcmp(f.cat.slot(1).text, "Are you OK?") == 0);
    CHECK(f.cat.slot(1).len == 11);
    // ⓘ `preset_slot_put` takes `size_t` for the same reason and CLAMPS — the last fence in front of the memcpy.
    mrnv::UiPresetSlot s{};
    mrfw::preset_slot_put(s, true, false, big, n273);
    CHECK(s.len == mrnv::kUiPresetTextMax);        // clamped, ⛔ never 273 bytes into an 18-byte buffer
    CHECK(mrfw::preset_slot_canonical(s, false));
}

// ==================================================== QG BLOCKER 2 — A LATE-ABSENT RECORD MUST REGAIN DURABILITY
TEST_CASE("ui10-p1-coalescing: ★ a LATE-ABSENT record makes the identical mutation WRITE — durability is restored") {
    // ★★★★ THE DURABILITY DEFECT, END TO END. A node is RUNNING a custom catalog (loaded from a valid record); a
    //      failed or partial storage operation then leaves `/mrui` ABSENT. The wearer re-enters the setting he can
    //      see on the panel. ⛔ The withdrawn shape compared the candidate against `_live` — RAM — found it equal,
    //      answered `unchanged`, wrote NOTHING, and lost the whole custom catalog at the next boot while telling him
    //      it was already stored. ★ The baseline must be the DURABLE side, and an absent record's durable side is
    //      the COMPILED DEFAULTS.
    Fix f;
    seed_valid(f.store);
    mrfw::preset_slot_put(f.store.rec.slot[4], true, true, "meet at the col", slen("meet at the col"));
    f.store.rec.slot[1].loc = 1;
    f.store.rec.generation = 44;
    CHECK(f.cat.begin() == mrnv::UiPresetRead::ok);
    CHECK(std::strcmp(f.cat.slot(4).text, "meet at the col") == 0);
    CHECK(f.cat.generation() == 44);

    // the storage operation that lost the record — the runtime catalog is untouched and still on screen
    f.store.state = mrnv::UiPresetRead::absent;
    CHECK(f.store.saves == 0);

    // ★ THE IDENTICAL MUTATION — byte for byte what the panel shows — MUST WRITE.
    const mrfw::PresetResult r = set_text(f.cat, 4, true, "meet at the col");
    CHECK(r.verdict == PresetVerdict::ok);          // ⛔ NOT `unchanged`
    CHECK(f.store.saves == 1);                      // ★ the record is durable again
    CHECK(f.cat.generation() == 45);
    CHECK(f.store.state == mrnv::UiPresetRead::ok);
    CHECK(std::strcmp(f.store.rec.slot[4].text, "meet at the col") == 0);
    CHECK(f.store.rec.slot[1].loc == 1);            // ★ the WHOLE custom catalog was restored, not just the one slot
    CHECK(mrfw::presets_canonical(f.store.rec));

    // ⓘ AND THE PIN-8 ABSENT ARM IS UNHARMED, which is what makes the fix a correction and not a repeal: when the
    //   running catalog IS what an absent record restores (the compiled defaults), a no-op still writes NOTHING.
    Fix g;
    g.store.state = mrnv::UiPresetRead::absent;
    g.cat.begin();
    CHECK(set_text(g.cat, 2, false, "I'm OK").verdict == PresetVerdict::unchanged);
    CHECK(g.store.saves == 0);
}

// ============================================================================== THE STABLE SLOT SPACE (§3.2.2/§B66)
TEST_CASE("ui10-p1-slots: the index IS the stable slot identity, and the ends both refuse") {
    CHECK(mrfw::preset_kind_of(0) == PresetKind::emergency);
    CHECK(mrfw::preset_ordinal_of(0) == 0);
    for (uint8_t i = 1; i <= 8; ++i) {
        CHECK(mrfw::preset_kind_of(i) == PresetKind::dm);
        CHECK(mrfw::preset_ordinal_of(i) == i);
        CHECK(mrfw::preset_slot_index(PresetKind::dm, i) == int(i));
    }
    for (uint8_t i = 9; i <= 16; ++i) {
        CHECK(mrfw::preset_kind_of(i) == PresetKind::channel);
        CHECK(mrfw::preset_ordinal_of(i) == uint8_t(i - 8));
        CHECK(mrfw::preset_slot_index(PresetKind::channel, i - 8) == int(i));
    }
    // ⛔ REFUSE, ⛔ never clamp (C2) — an ordinal this catalog does not have is not "the nearest slot".
    CHECK(mrfw::preset_slot_index(PresetKind::dm, 0) == -1);
    CHECK(mrfw::preset_slot_index(PresetKind::dm, 9) == -1);
    CHECK(mrfw::preset_slot_index(PresetKind::channel, 0) == -1);
    CHECK(mrfw::preset_slot_index(PresetKind::channel, 9) == -1);
    CHECK(mrfw::preset_slot_index(PresetKind::emergency, 1) == -1);
    CHECK(mrfw::preset_slot_index(PresetKind::emergency, 0) == 0);
    CHECK(mrfw::preset_slot_index(PresetKind::count, 1) == -1);
    // both off-by-one ends of the index space
    CHECK_FALSE(mrfw::preset_slot_valid(-1));
    CHECK(mrfw::preset_slot_valid(0));
    CHECK(mrfw::preset_slot_valid(16));
    CHECK_FALSE(mrfw::preset_slot_valid(17));
    CHECK_FALSE(mrfw::preset_slot_valid(4000000000L));
    CHECK(mrfw::preset_slot_mandatory(0));
    for (uint8_t i = 1; i < mrnv::kUiPresets; ++i) CHECK_FALSE(mrfw::preset_slot_mandatory(i));

    Fix f;
    seed_valid(f.store);
    f.cat.begin();
    for (long bad : {-1L, 17L, 4000000000L}) {
        CHECK(set_text(f.cat, bad, false, "x").err == PresetErr::bad_slot);
        CHECK(f.cat.clear(bad).err == PresetErr::bad_slot);
        CHECK(f.cat.reset_slot(bad).err == PresetErr::bad_slot);
    }
    CHECK(f.store.saves == 0);
    CHECK(f.store.loads == 1);   // ⛔ ZERO loads for every one of those refusals
    // ★ A GAP IS VALID (§3.2.2's own example): dm1, dm4 and dm8 enabled, the rest not — and each row keeps ITS id.
    CHECK(f.cat.clear(2).verdict == PresetVerdict::ok);
    CHECK(set_text(f.cat, 4, false, "at the col").verdict == PresetVerdict::ok);
    CHECK(set_text(f.cat, 8, true, "descending").verdict == PresetVerdict::ok);
    CHECK(f.cat.enabled_count(PresetKind::dm) == 3);
    CHECK(f.cat.slot(1).enabled == 1);
    CHECK(f.cat.slot(2).enabled == 0);
    CHECK(f.cat.slot(4).enabled == 1);
    CHECK(f.cat.slot(8).enabled == 1);
    CHECK(f.cat.enabled_count(PresetKind::channel) == 2);   // ⛔ the DM edits touched no channel slot
    // ⛔ AND THE ACCESSOR NEVER READS PAST THE ARRAY: an out-of-range index answers the emergency slot.
    CHECK(std::strcmp(f.cat.slot(200).text, f.cat.slot(0).text) == 0);
}

TEST_CASE("ui10-p1-slots: zero enabled per kind is representable — P3's empty state has something to render") {
    Fix f;
    seed_valid(f.store);
    f.cat.begin();
    for (uint8_t i = 1; i <= 16; ++i) if (f.cat.slot(i).enabled) CHECK(f.cat.clear(i).verdict == PresetVerdict::ok);
    CHECK(f.cat.enabled_count(PresetKind::dm) == 0);
    CHECK(f.cat.enabled_count(PresetKind::channel) == 0);
    CHECK(f.cat.slot(0).enabled == 1);          // ⛔ the emergency slot survives an empty catalog
    CHECK(mrfw::presets_canonical(f.store.rec));
    // `reset all` brings the compiled catalog back.
    CHECK(f.cat.reset_all().verdict == PresetVerdict::ok);
    CHECK(f.cat.enabled_count(PresetKind::dm) == 2);
    CHECK(f.cat.enabled_count(PresetKind::channel) == 2);
}

// ================================================================================ THE ENUM INVENTORIES AND WORDS
TEST_CASE("ui10-p1-words: every enum arm is worded, distinct, and swept BY CONSTRUCTION") {
    // ★★ THE SWEEP ITERATES `0 .. count-1`, so an arm added above a sentinel is visited by construction; the
    //    `default`-less switches make an arm added and NOT worded a BUILD FAILURE under -Werror=switch.
    for (uint8_t i = 0; i < uint8_t(PresetKind::count); ++i) {
        const char* w = mrfw::preset_kind_name(PresetKind(i));
        CHECK(w != nullptr);
        CHECK(std::strcmp(w, "?") != 0);
        for (uint8_t j = 0; j < i; ++j) CHECK(std::strcmp(w, mrfw::preset_kind_name(PresetKind(j))) != 0);
    }
    CHECK(std::strcmp(mrfw::preset_kind_name(PresetKind::count), "?") == 0);   // ⛔ the sentinel is not a kind

    for (uint8_t i = 0; i < uint8_t(PresetVerdict::count); ++i) {
        const char* w = mrfw::preset_verdict_name(PresetVerdict(i));
        CHECK(std::strcmp(w, "?") != 0);
        for (uint8_t j = 0; j < i; ++j) CHECK(std::strcmp(w, mrfw::preset_verdict_name(PresetVerdict(j))) != 0);
    }
    CHECK(std::strcmp(mrfw::preset_verdict_name(PresetVerdict::count), "?") == 0);

    for (uint8_t i = 0; i < uint8_t(PresetErr::count); ++i) {
        const char* w = mrfw::preset_err_name(PresetErr(i));
        CHECK(std::strcmp(w, "?") != 0);
        for (uint8_t j = 0; j < i; ++j) CHECK(std::strcmp(w, mrfw::preset_err_name(PresetErr(j))) != 0);
    }
    CHECK(std::strcmp(mrfw::preset_err_name(PresetErr::count), "?") == 0);
    // ★★★ THE SIX REASONS ARE §3.2.3's `ui_preset_err` SET, COMPLETE, spelled as the NDJSON will carry them (P2
    //     renders these verbatim). ⛔ CORRECTED 2026-08-25 (QG blocker 3), and the WITHDRAWN CLAIM IS KEPT VISIBLE:
    //     this line read `== 6` and its comment said *"none + the five this service can produce"* — which was the
    //     enum SHORT ONE REASON while asserting the published set was complete. `bad_location` is now declared;
    //     ⛔ P1 has no producer for it and does not pretend to (its producer is P2's `loc=<on|off>` parser), so it
    //     appears HERE — in the inventory and the wording — and in no service-level outcome case.
    CHECK(uint8_t(PresetErr::count) == 7);      // none + §3.2.3's SIX reasons
    CHECK(std::strcmp(mrfw::preset_err_name(PresetErr::bad_slot), "bad_slot") == 0);
    CHECK(std::strcmp(mrfw::preset_err_name(PresetErr::bad_text), "bad_text") == 0);
    CHECK(std::strcmp(mrfw::preset_err_name(PresetErr::bad_location), "bad_location") == 0);
    CHECK(std::strcmp(mrfw::preset_err_name(PresetErr::mandatory), "mandatory") == 0);
    CHECK(std::strcmp(mrfw::preset_err_name(PresetErr::busy), "busy") == 0);
    CHECK(std::strcmp(mrfw::preset_err_name(PresetErr::store), "store") == 0);
    // the default-constructed result is the SAFE one: a refusal that changed nothing
    const mrfw::PresetResult r{};
    CHECK(r.verdict == PresetVerdict::refused);
    CHECK(r.err == PresetErr::none);
}

// ============================================================================== §5 — THE RESOURCE / STACK GATE
TEST_CASE("ui10-p1-resources: the catalog's residency is MEASURED, and ⛔ no full record rides a stack") {
    // ★★★ THE OWNER-RULED STACK GATE (spec §5). The service holds THREE records — the LIVE catalog plus TWO scratch
    //     (the candidate it composes and the record it read, which are exactly the two the byte-identical compare is
    //     between). ⇒ 1116 B RESIDENT and ⛔ ZERO bytes of catalog on any stack, on any path.
    //     ⛔ The rejected alternative, stated with its arithmetic: as stack locals the two scratch records would be
    //     744 B in the mutating frame — 18 % of the nRF52 Arduino loop task's FIXED 4 KB, on which this tree has
    //     already HARDFAULTED once with `stackhw` down to 72 B (the `do_post_ack` frame). `begin()` runs from
    //     `setup()` on exactly that task.
    CHECK(sizeof(mrnv::UiPresetBlob) == 372);
    CHECK(3 * sizeof(mrnv::UiPresetBlob) == 1116);
    // The service is its three records plus two references and three counters — ⛔ no hidden fourth copy.
    CHECK(sizeof(mrfw::PresetCatalog) >= 3 * sizeof(mrnv::UiPresetBlob));
    CHECK(sizeof(mrfw::PresetCatalog) <= 3 * sizeof(mrnv::UiPresetBlob) + 64);
    // The flash record's size IS the migration policy (`load_ui_presets`' exact size check), pinned per-ABI.
    CHECK(sizeof(mrnv::UiPresetBlob) == 12 + mrnv::kUiPresets * sizeof(mrnv::UiPresetSlot) + 3);
}
