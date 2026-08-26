// MeshRoute — src/firmware_ui_preset_verbs.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-10/UI-11 slice P2 — THE `ui preset` VERB FAMILY: its grammar, its THREE NDJSON records and the boot
// diagnosis, as a PURE unit. Specs: the parent design
// docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md §3.2.3 (the grammar and the three records,
// VERBATIM — that block is the authority for every byte below) over
// docs/superpowers/specs/2026-08-25-ui10-11-preset-catalog-spec.md §2 (the owner's `busy` table + the
// documentation rule) and §3-P2 (the slice row).
//
// ★★ WHY THE DECISIONS ARE HERE AND ⛔ NOT IN `firmware_commands.cpp`, and it is P1's argument one layer up
//    (§B115, measured, not assumed): `src/firmware_commands.cpp`, `src/firmware_config.cpp` and
//    `src/fw_main.cpp` are compiled by NEITHER the native suite (`test_build_src = no`) NOR the simulator, and
//    no corpus scenario runs a console verb or a boot. ⇒ a record's BYTES, a reason SPELLING, the stable slot
//    ORDER or the boot LINE decided there would be unreachable by every automated gate — which is exactly how
//    this project shipped three enum->string defects the byte-identity gate was structurally blind to.
//    ⇒ this header is pure — ⛔ no `Print`, ⛔ no Arduino, ⛔ no globals — so
//    `test/test_firmware_ui_preset_verbs.cpp` can pin every emitted byte and `--target=uipresetverbs` can
//    attack every decision at match count 1. What is left in the device TU is: adapt a `Print`, hold the ONE
//    instance, call.
//
// ★★★ THE EMISSION SEAM IS `IPresetLines`, AND IT IS **THE ONE-DISPATCH PROOF MADE STRUCTURAL**. §3.2.3 rules
//     that *"USB serial and BLE return the same bounded NDJSON records"*. The tree already routes both
//     transports through ONE `mrfw::dispatch(line, len, Print&)` (`src/fw_main.cpp:990` = USB,
//     `src/fw_main.cpp:549` = the BLE `unknown_verb` fallback through a `LineSink`), so the two differ ONLY in
//     the sink OBJECT — never in a code path. ⇒ everything below composes bytes and hands them to a sink it
//     cannot inspect: there is no `if (ble)` to write, because there is nothing here that could ask.
//     ⛔ A BLE-side interception of `ui …` in `ble_dispatch_line` would fork that — which is why the slice adds
//     NONE, and why `test_firmware_ui_preset_verbs.cpp` drives the SAME call through a direct sink and through
//     a line-splitting/re-assembling sink and requires the byte streams to be equal.
//
// ⓘ WHAT THIS FILE DELIBERATELY DOES **NOT** HAVE (per [[meshroute-mark-done-vs-missing-in-code]]):
//     · ⛔ NO COMPOSE-LIST PROJECTION, ⛔ no `SendReq` change, ⛔ no `PRESET CHANGED` refusal, ⛔ no modal
//       close. All of those are **P3**. What P3 needs from P2 is a FACT, and the fact already exists and is
//       the design's own: `PresetCatalog::generation()`. A successful durable mutation stamps the NEXT
//       generation into the record before the save (P1's `commit`), so a frozen frame's sealed generation
//       stops comparing equal the instant a change lands — which is precisely the *"a preset update while a
//       selection-phase compose modal is open closes that modal"* trigger and the stale-`SendReq` refusal,
//       from ONE fact. ⇒ this slice threads NO new hook and ⛔ touches no compose code.
//       ✅ **LANDED 2026-08-26 (P3), AND THE THREADING DECISION HELD EXACTLY AS WRITTEN** — the marker is UPDATED
//       IN PLACE, ⛔ not deleted (the rule cuts both ways). P3 added ⛔ NO hook to this file and ⛔ no field to
//       `PresetCatalog`: the modal close is `mrui::UiModel::preset_generation_moved`, comparing
//       `UiState::compose_gen` against the published `UiSnapshot::preset_generation`, and the stale-`SendReq`
//       refusal is `mrui::send_gate_of` (`src/firmware_ui_send.h`), comparing the SEALED generation against the
//       LIVE record at execution. Both descend from `PresetCatalog::generation()` — one through
//       `build_snapshot`'s single publication, one through `preset_catalog().live()` — and from nothing else.
//       ⇒ *"from ONE fact"*, measured rather than hoped for.
//     · ⛔ NO STORE. The record, its four-state read and its whole write policy are P1's
//       (`src/firmware_ui_presets.h`), consumed here and ⛔ not re-decided.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>          // snprintf — the slot token
#include <cstring>         // memcmp/memcpy/strlen
#include "firmware_ui_presets.h"   // mrfw::PresetCatalog + the typed verdicts/reasons P1 published for THIS slice
#include "console_json.h"          // meshroute::console::JsonBuf — THE tree's bounded NDJSON writer (U1)

namespace mrfw {

// ---- the emission seam ------------------------------------------------------------------------------------------
// ★ ONE method, and the contract is *"these bytes are a COMPLETE line and already carry their '\n'"* — the shape
//   `LineSink` (src/dispatch_sink.h) and every existing `out.write(s_inbox_jb, m)` call site already have. A sink
//   that buffers to a newline therefore ships exactly one record per notification, unchanged.
// ⛔ IT IS NOT A `Print`: a `Print` drags `<Arduino.h>` in and this header would stop being host-compilable — the
//    one property that makes every byte below attackable (§B115).
struct IPresetLines {
    virtual ~IPresetLines() = default;
    virtual void line(const char* s, size_t n) = 0;
};

// ★★ THE LINE BUFFER, SIZED FROM THE RECORD'S OWN MAXIMUM rather than guessed. The widest record this file can
//    produce is a `ui_preset` for `channel8` with a full 17-byte text:
//      {"ev":"ui_preset","slot":"channel8","enabled":false,"text":"<17>","location":false}\n  = 98 B + NUL.
//    The widest `ui_presets_end` is 96 B + NUL (`generation` at 4294967295), and `ui_preset_err` is far shorter.
// ⓘ NO ESCAPE GROWTH IS POSSIBLE: `validate_preset_text` refuses `"` and `\` and everything outside 0x20..0x7e, and
//   `presets_canonical` refuses a stored record that breaks that — so `JsonBuf::str` copies 17 bytes as 17 bytes.
//   The margin below is for a FUTURE field, ⛔ not for an escape nobody can produce.
inline constexpr size_t kPresetLineMax = 160;

// ---- the stable slot TOKEN (§3.2.3's `emergency` / `dm1..dm8` / `channel1..channel8`) ----------------------------
// ★ COMPOSED FROM P1's `preset_kind_name` + `preset_ordinal_of` (U1), ⛔ never a second table of seventeen strings:
//   the kind namer is the `-Werror=switch`-fenced one, and the ordinal is the record's own index arithmetic. A
//   hand-written token table is how `dm3` and slot 3 would eventually disagree — §B66's lesson, one record over.
// ⓘ `emergency` HAS NO ORDINAL (`preset_ordinal_of` answers 0 for it, deliberately — it is one slot, not the first
//   of a series), which is what selects the one-argument form.
static_assert(kPresetPerKind <= 9, "firmware_ui_preset_verbs.h: a two-digit ordinal needs a wider token parse");
inline size_t preset_slot_token(uint8_t slot, char* out, size_t cap) {
    const PresetKind k = preset_kind_of(slot);
    const uint8_t    o = preset_ordinal_of(slot);
    const int n = (o == 0) ? snprintf(out, cap, "%s", preset_kind_name(k))
                           : snprintf(out, cap, "%s%u", preset_kind_name(k), static_cast<unsigned>(o));
    return n > 0 ? static_cast<size_t>(n) : 0u;
}

// The inverse — the `set`/`clear`/`reset` argument. ⛔ Returns -1 rather than clamping or guessing (C2): a token this
// catalog does not have is `bad_slot`, never the nearest slot.
// ★ THE ORDINAL IS EXACTLY ONE DIGIT, and that is a REFUSAL rule, not a parsing convenience: `dm01`, `dm+1`, `dm10`
//   and `dm1x` are all REFUSED. A lenient parse would accept `dm10` as `dm1` and silently edit the wrong slot.
inline long preset_slot_of_token(const char* t, size_t n) {
    if (!t) return -1;
    if (n == 9 && !memcmp(t, "emergency", 9)) return preset_slot_index(PresetKind::emergency, 0);
    PresetKind k = PresetKind::dm;
    size_t     pre = 0;
    if      (n > 2 && !memcmp(t, "dm", 2))      { k = PresetKind::dm;      pre = 2; }
    else if (n > 7 && !memcmp(t, "channel", 7)) { k = PresetKind::channel; pre = 7; }
    else return -1;
    if (n - pre != 1) return -1;                       // ⛔ exactly one digit — see the block above
    const char c = t[pre];
    if (c < '0' || c > '9') return -1;
    return preset_slot_index(k, c - '0');              // ⛔ P1's inverse: ordinal 0 and 9.. are REFUSED there (U1)
}

// ---- `loc=<on|off>` — ★★★ **THE PRODUCER OF `PresetErr::bad_location`** ------------------------------------------
// P1 declared that arm with NO producer and recorded the debt in `firmware_ui_presets.h`'s `PresetErr` block
// (*"ITS PRODUCER IS **P2's `loc=<on|off>` PARSER**"*); this function closes it. The service takes `loc` as a
// `bool`, so every value it can be HANDED is a valid location — an invalid location can only exist BEFORE the
// parse, which is why the arm belongs here and could not have belonged there.
// ⛔ EXACTLY TWO SPELLINGS AND NOTHING ELSE (C2): `loc=maybe`, `loc=1`, `loc=ON`, `loc=` and a missing `loc=` all
//    REFUSE. ⛔ No third value, ⛔ no default-when-absent — a preset that quietly aired coordinates the wearer did
//    not ask for is the whole reason `include_location` is an EXPLICIT boolean in §3.2.2.
inline bool preset_parse_loc(const char* t, size_t n, bool& out) {
    if (t && n == 6 && !memcmp(t, "loc=on",  6)) { out = true;  return true; }
    if (t && n == 7 && !memcmp(t, "loc=off", 7)) { out = false; return true; }
    return false;
}

// ---- THE THREE NDJSON RECORDS, §3.2.3 VERBATIM ------------------------------------------------------------------
// ★★★ THE BYTES ARE THE CONTRACT, field for field and in the design's own ORDER, because an iOS companion parses
//     them and `ios-companion/INBOX_SYNC_CONTRACT.md` publishes them. The design block, quoted so a re-wording has
//     to disagree with something visible:
//       {"ev":"ui_preset","slot":"dm1","enabled":true,"text":"Are you OK?","location":false}
//       {"ev":"ui_presets_end","capacity":17,"dm_active":2,"channel_active":2,"generation":7}
//       {"ev":"ui_preset_err","reason":"bad_slot|bad_text|bad_location|mandatory|busy|store"}
// ⓘ `JsonBuf` is the tree's writer (U1) — bounded, heap-free, overflow-latching, and `finish()` appends the '\n'
//   that makes these NDJSON. ⛔ Not a hand-rolled snprintf per record: that is how a quote or a comma drifts.
inline size_t write_ui_preset(char* buf, size_t cap, uint8_t slot, const mrnv::UiPresetSlot& s) {
    char tok[16];
    preset_slot_token(slot, tok, sizeof tok);
    meshroute::console::JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"ui_preset\",\"slot\":\""); j.lit(tok); j.ch('"');
    j.lit(",\"enabled\":");  j.lit(s.enabled ? "true" : "false");
    // ★ A DISABLED SLOT RENDERS `"text":""` AND `"location":false`, by construction rather than by a special case:
    //   P1's canonical rule zeroes a disabled slot whole (`preset_slot_put`), so `len` is 0 and `loc` is 0 here.
    j.lit(",\"text\":");     j.str(s.text, s.len);
    j.lit(",\"location\":"); j.lit(s.loc ? "true" : "false");
    j.ch('}');
    return j.finish();
}
// ★ `capacity` IS `mrnv::kUiPresets`, ⛔ NEVER A LITERAL 17. The design says *"this lets the companion edit exact
//   stable slots"* — so the number the companion sizes its editor from must be the number the record actually has,
//   and the ONLY way that can never drift is to read it from the record's own constant.
inline size_t write_ui_presets_end(char* buf, size_t cap, uint8_t dm_active, uint8_t channel_active,
                                   uint32_t generation) {
    meshroute::console::JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"ui_presets_end\",\"capacity\":"); j.u32(mrnv::kUiPresets);
    j.lit(",\"dm_active\":");      j.u32(dm_active);
    j.lit(",\"channel_active\":"); j.u32(channel_active);
    // ★★ THE GENERATION RIDES THE END RECORD, and it is not decoration: §3.2.3 makes it the equality token a
    //    `SendReq` seals and a companion editor re-reads. A list without it cannot tell the reader WHICH catalog
    //    it just described.
    j.lit(",\"generation\":");     j.u32(generation);
    j.ch('}');
    return j.finish();
}
// ★★ THE SIX REASONS ARE SPELLED BY P1's `preset_err_name` (U1) — ⛔ never re-spelled here. That mapper is
//    `default`-less and `-Werror=switch`-fenced, so an owner re-wording changes ONE place and a native case sees
//    it. ⓘ `none` and the `count` sentinel HAVE no producer on this path; they are deliberately NOT special-cased,
//    so a future defect that reached here would emit a VISIBLE `"reason":"none"` rather than be swallowed by a
//    silent early return no mutation could redden (P1's `crypto_wipe -> memset` lesson).
inline size_t write_ui_preset_err(char* buf, size_t cap, PresetErr e) {
    meshroute::console::JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"ui_preset_err\",\"reason\":\""); j.lit(preset_err_name(e)); j.ch('"');
    j.ch('}');
    return j.finish();
}

// ---- the emitters -----------------------------------------------------------------------------------------------
// ★ THE ONE PLACE BYTES REACH A SINK. Every record below goes through it, which is what makes the "USB and BLE
//   byte-agree" property a property of ONE statement instead of a promise about two transports.
inline void preset_emit(IPresetLines& out, const char* buf, size_t n) { out.line(buf, n); }

inline void preset_emit_record(const PresetCatalog& cat, uint8_t slot, IPresetLines& out) {
    char b[kPresetLineMax];
    preset_emit(out, b, write_ui_preset(b, sizeof b, slot, cat.slot(slot)));
}
// ★★★ `list` = **ALL SEVENTEEN, IN STABLE SLOT ORDER, INCLUDING THE DISABLED ONES**, then `ui_presets_end`
//     (§3.2.3, verbatim: *"this lets the companion edit exact stable slots without inferring them from
//     active-list positions"*). ⛔ Filtering the disabled rows out is the defect this loop exists to prevent: an
//     editor that only ever sees the enabled slots cannot address `dm5` to turn it ON, and it would have to infer
//     `dmN` from a list position — §B66's exact cure, undone one layer up.
inline void preset_emit_list(const PresetCatalog& cat, IPresetLines& out) {
    for (uint8_t i = 0; i < mrnv::kUiPresets; ++i) preset_emit_record(cat, i, out);
    char b[kPresetLineMax];
    preset_emit(out, b, write_ui_presets_end(b, sizeof b,
                                             cat.enabled_count(PresetKind::dm),
                                             cat.enabled_count(PresetKind::channel),
                                             cat.generation()));
}
inline void preset_emit_err(PresetErr e, IPresetLines& out) {
    char b[kPresetLineMax];
    preset_emit(out, b, write_ui_preset_err(b, sizeof b, e));
}

// ---- the retained storage DIAGNOSIS (the design's *"visible boot/status warning"*) --------------------------------
// ★★ P1 owns the two exact lines and the `nullptr` third answer (`preset_boot_line`); this owns WHEN THEY STOP
//    BEING TRUE, which is a decision and therefore belongs in a unit a case can drive.
//   · a boot read of `invalid` is a fault the wearer must see — and the owner ruled it REPAIRABLE: *"a later
//     successful mutation MAY rewrite the complete canonical catalog and repair it"*. ⇒ the first `ok` verdict
//     CLEARS the warning, because at that moment the record on flash IS canonical again and the line would
//     otherwise go on claiming a fault that has been fixed.
//   · a boot read of `io_failed` can NEVER be cleared here, and that too is by construction rather than by a
//     rule: every mutation over an unreadable store returns `store` with zero writes, so no `ok` verdict exists
//     to clear it. ⛔ Do not "fix" that by clearing on any other verdict.
// ⓘ The initial value is `absent`, the SILENT state — so a read before the boot restore can never invent a
//   warning (`preset_boot_line(absent)` is `nullptr`).
struct PresetDiag {
    mrnv::UiPresetRead boot = mrnv::UiPresetRead::absent;
    void on_boot(mrnv::UiPresetRead st) { boot = st; }
    void on_result(const PresetResult& r) { if (r.verdict == PresetVerdict::ok) boot = mrnv::UiPresetRead::ok; }
    const char* line() const { return preset_boot_line(boot); }   // ⛔ P1's words, ⛔ never re-worded here (U1)
};

// ---- THE BOOT PATH ----------------------------------------------------------------------------------------------
// ★★ THE WHOLE BOOT DECISION IS THESE FIVE LINES, and they live here rather than in `setup()` for §B115's reason:
//    `src/fw_main.cpp` gains a CALL, ⛔ never a decision (the `mrfw::peer_store_restore()` /
//    `mrfw::team_keyring_restore_boot()` idiom, U3).
// ⓘ The line is copied into a buffer with its '\n' because `IPresetLines` carries COMPLETE lines; `preset_boot_line`
//   returns bare text so that P1's constant stays a lexeme a native case can compare, not a formatted line.
inline mrnv::UiPresetRead preset_boot_restore(PresetCatalog& cat, PresetDiag& diag, IPresetLines& out) {
    const mrnv::UiPresetRead st = cat.begin();   // ⛔ ZERO writes on every arm — P1's `begin()` never repairs
    diag.on_boot(st);
    const char* ln = diag.line();
    if (!ln) return st;                          // ★ `ok` / `absent` print NOTHING — an ordinary first boot is silent
    char b[kPresetLineMax];
    size_t n = strlen(ln);
    if (n > sizeof b - 2) n = sizeof b - 2;      // last fence in front of a memcpy bound; both ruled lines are ~78 B
    memcpy(b, ln, n);
    b[n] = '\n';
    preset_emit(out, b, n + 1);
    return st;
}

// ---- the grammar ------------------------------------------------------------------------------------------------
// A non-mutating cursor over the argument text. ⛔ No `strtok`, ⛔ no mutable copy: `dispatch` hands a `const char*`
// that BORROWS the transport's live line buffer (the same rule `Command::body` carries), and a 512-B static copy is
// what `handle_testsched` had to do only because `strtok` demanded one.
struct PresetArgs {
    const char* p;
    const char* end;
    PresetArgs(const char* a, size_t n) : p(a), end(a + n) {}
    // The next whitespace-delimited word, or nullptr at the end.
    bool word(const char*& t, size_t& n) {
        while (p < end && *p == ' ') ++p;
        if (p >= end) return false;
        t = p;
        while (p < end && *p != ' ') ++p;
        n = static_cast<size_t>(p - t);
        return true;
    }
    // ★ THE QUOTED TEXT TERM. Opens on `"` and closes on the NEXT `"` — ⛔ never the last one on the line: a phrase
    //   may not contain a quote (`validate_preset_text` refuses it), so a second pair is a MALFORMED line and must
    //   be refused as trailing rubbish rather than silently absorbed.
    bool quoted(const char*& t, size_t& n) {
        while (p < end && *p == ' ') ++p;
        if (p >= end || *p != '"') return false;
        ++p;
        t = p;
        while (p < end && *p != '"') ++p;
        if (p >= end) return false;              // ⛔ unterminated -> NOT a text, C2
        n = static_cast<size_t>(p - t);
        ++p;
        return true;
    }
    // ⛔ A TRAILING TOKEN IS REFUSED, ⛔ NOT IGNORED (C2, and `handle_joinprofile`'s own rule): `ui preset list all`
    //   silently running a plain `list` is how an operator comes to believe he asked a question he did not ask.
    bool exhausted() {
        while (p < end && *p == ' ') ++p;
        return p >= end;
    }
};
inline bool preset_word_is(const char* t, size_t n, const char* lit) {
    const size_t m = strlen(lit);
    return n == m && !memcmp(t, lit, m);
}

// ★★★ THE RESULT -> OUTPUT RULE, §3.2.3 VERBATIM: *"Mutating verbs return the resulting record, or the full list
//     for `reset all`."* ⛔ NOT a dump for a single-slot verb (the companion would have to diff seventeen records
//     to find what it just changed) and ⛔ NOT a bare record for `reset all` (there is no ONE slot it changed).
// ★ `unchanged` RENDERS THE RECORD TOO, and that is the design's word "resulting" taken literally: the verb
//   succeeded, it simply cost no flash. A companion must see the same answer for "I set what was already set" as
//   for "I set something new", or it will re-issue the write forever chasing a reply that never comes.
// ⓘ `default`-less, so `-Werror=switch` fails the build when a `PresetVerdict` arm is added without deciding
//   which of the two shapes it takes (the `joinprofile_refusal_needs_usage` discipline).
inline void preset_render(const PresetCatalog& cat, long slot, bool whole_list, const PresetResult& r,
                          IPresetLines& out) {
    switch (r.verdict) {
        case PresetVerdict::ok:
        case PresetVerdict::unchanged:
            if (whole_list) preset_emit_list(cat, out);
            else            preset_emit_record(cat, static_cast<uint8_t>(slot), out);
            return;
        case PresetVerdict::refused:
        case PresetVerdict::nv_failed:
            // ⓘ `nv_failed` carries `PresetErr::store` from P1 — the design's sixth reason, ⛔ not a seventh.
            preset_emit_err(r.err, out);
            return;
        case PresetVerdict::count:
            return;   // ⛔ the sentinel is not an outcome — spelled so a real arm added above cannot be swallowed
    }
}

// ★★★★ THE VERB FAMILY. Returns `false` — and emits NOTHING — when the line is not this grammar at all; the caller
//      prints the usage line. `true` means the family ANSWERED, in NDJSON.
// ★★ THE ORDER INSIDE EACH MUTATING VERB IS DELIBERATE AND IS **NOT** A SECOND `busy` GATE:
//      parse the arguments -> hand the well-formed request to the service -> render.
//    The service's OWN first question is `_gate.emergency_active()` (P1), so every WELL-FORMED mutating verb is
//    answered `busy` during an alarm — including a no-op `set`, which is the owner's ruled row and the one a
//    "skip the store when nothing changed" optimisation would break. ⛔ A malformed line is refused HERE, before
//    the catalog is touched at all: it costs the same ZERO loads and ZERO writes, and answering `busy` to a typo
//    would hide the typo behind a state the operator cannot see. ⇒ ⛔ do not add a gate query to this file: a
//    second copy of that fact is a copy that can disagree (P1's handling-time discipline).
inline bool preset_verb(PresetCatalog& cat, PresetDiag& diag, const char* args, size_t len, IPresetLines& out) {
    PresetArgs a(args, len);
    const char* t = nullptr; size_t n = 0;
    if (!a.word(t, n) || !preset_word_is(t, n, "preset")) return false;
    if (!a.word(t, n)) return false;                                   // `ui preset` alone -> the usage line

    if (preset_word_is(t, n, "list")) {
        if (!a.exhausted()) return false;                              // C2 — a trailing token is a MISTYPE
        preset_emit_list(cat, out);
        return true;
    }
    if (preset_word_is(t, n, "set")) {
        const char* st = nullptr; size_t sn = 0;
        const char* lt = nullptr; size_t ln = 0;
        const char* xt = nullptr; size_t xn = 0;
        if (!a.word(st, sn) || !a.word(lt, ln)) return false;          // too few terms -> the grammar, not a reason
        // ★ THE THIRD TERM'S ABSENCE IS THE GRAMMAR; ITS MALFORMEDNESS IS `bad_text`, and the split is deliberate.
        //   `ui preset set dm1 loc=on` is an INCOMPLETE line and the operator needs the shape back; `… loc=on
        //   unquoted` and `… loc=on ""` are a text term he got WRONG, and a companion needs the machine-readable
        //   reason. ⛔ Collapsing the two would either bury a typo under a reason code or hide a real `bad_text`
        //   behind a usage dump.
        if (a.exhausted()) return false;
        const long slot = preset_slot_of_token(st, sn);
        if (slot < 0)                          { preset_emit_err(PresetErr::bad_slot, out);     return true; }
        bool loc = false;
        if (!preset_parse_loc(lt, ln, loc))    { preset_emit_err(PresetErr::bad_location, out); return true; }
        if (!a.quoted(xt, xn))                 { preset_emit_err(PresetErr::bad_text, out);     return true; }
        if (!a.exhausted()) return false;                              // C2 — rubbish after the closing quote
        // ★ `size_t` ALL THE WAY TO THE SERVICE — P1's QG blocker 1: narrowing at this boundary would turn a
        //   273-byte phrase into 17 and every check inside the service would agree it was fine.
        const PresetResult r = cat.set(slot, loc, xt, xn);
        diag.on_result(r);
        preset_render(cat, slot, /*whole_list=*/false, r, out);
        return true;
    }
    if (preset_word_is(t, n, "clear")) {
        const char* st = nullptr; size_t sn = 0;
        if (!a.word(st, sn)) return false;
        if (!a.exhausted()) return false;                              // C2 — `clear dm1 now` is a MISTYPE
        const long slot = preset_slot_of_token(st, sn);
        if (slot < 0) { preset_emit_err(PresetErr::bad_slot, out); return true; }
        // ⛔ `clear emergency` IS REFUSED BY THE SERVICE, ⛔ not filtered here: `mandatory` is one of the six
        //    reasons and P1 owns which slot is mandatory (`preset_slot_mandatory`). A second opinion in the parser
        //    is exactly the fork U1 forbids — and it would answer `mandatory` even during an alarm, where the
        //    ruled answer is `busy`.
        const PresetResult r = cat.clear(slot);
        diag.on_result(r);
        preset_render(cat, slot, /*whole_list=*/false, r, out);
        return true;
    }
    if (preset_word_is(t, n, "reset")) {
        const char* st = nullptr; size_t sn = 0;
        if (!a.word(st, sn)) return false;
        if (!a.exhausted()) return false;                              // C2 — `reset all confirm` is a MISTYPE
        if (preset_word_is(st, sn, "all")) {
            const PresetResult r = cat.reset_all();
            diag.on_result(r);
            preset_render(cat, 0, /*whole_list=*/true, r, out);         // §3.2.3: `reset all` answers with the LIST
            return true;
        }
        const long slot = preset_slot_of_token(st, sn);
        if (slot < 0) { preset_emit_err(PresetErr::bad_slot, out); return true; }
        // ⓘ `reset emergency` is ALLOWED where `clear emergency` is refused: restoring the compiled phrase is a TEXT
        //   EDIT, not a disable (P1's `reset_slot` note). The asymmetry is the design's, not this parser's.
        const PresetResult r = cat.reset_slot(slot);
        diag.on_result(r);
        preset_render(cat, slot, /*whole_list=*/false, r, out);
        return true;
    }
    return false;   // an unknown sub-verb -> the usage line, ⛔ never a silent no-op
}

}  // namespace mrfw
