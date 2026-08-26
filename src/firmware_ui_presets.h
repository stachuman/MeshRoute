// MeshRoute — src/firmware_ui_presets.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-10/UI-11 slice P1 — THE `/mrui` PRESET CATALOG SERVICE. Specs:
// docs/superpowers/specs/2026-08-25-ui10-11-preset-catalog-spec.md §2 (owner rulings) + §3-P1 (the slice row,
// owner-approved 2026-08-25, QA round 2 folded in), over the parent design
// docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md §3.2.2 (the catalog table) / §3.2.3 (location
// semantics, verbs and persistence).
//
// ★★★ WHAT THIS SLICE IS: the hard-coded compose strings stop being FIRMWARE POLICY and become DEFAULTS. The wearer's
//     seventeen slots — one mandatory `emergency`, eight `dm`, eight `channel` — live in a record of their own, and
//     this header is where every rule about them is decided and can be ATTACKED.
//
// ★★ WHY THE DECISIONS ARE HERE AND NOT IN A VERB / IN `fw_main`, for the same measured reason K1 and the `/mrjoin`
//    store both give: `src/firmware_config.cpp`, `src/firmware_commands.cpp` and `src/fw_main.cpp` are compiled by
//    NEITHER the native suite (`test_build_src = no`) NOR the simulator, and no corpus scenario runs a console verb
//    or a boot (§B115). ⇒ a write count, a validation bound or a canonical-byte rule decided there would be
//    unreachable by EVERY automated gate. This header is pure — no `Print`, no Arduino, no globals — so
//    `test/test_firmware_ui_presets.cpp` can COUNT the writes and DRIVE every state.
//
// ★★★ THE FOUR STORAGE STATES, OWNER-RULED (spec §3-P1), each pinned by a case and attacked by a mutation. They are
//     FOUR because collapsing any pair tells the wearer the wrong thing about phrases he configured:
//       · absent    ⇒ compiled defaults, ⛔ NO warning          — an ordinary first boot is not a fault
//       · invalid   ⇒ defaults + a COUNTED, VISIBLE warning     — his phrases are GONE and the panel is showing
//                                                                 texts he did not choose; a later SUCCESSFUL
//                                                                 mutation rewrites the complete canonical catalog
//                                                                 and REPAIRS the record
//       · io_failed ⇒ defaults + a DISTINCT warning, and ★ EVERY mutation returns `store` with ⛔ ZERO writes —
//                     nothing is known about the record, and a blind rewrite would destroy a POSSIBLY-INTACT
//                     catalog because a mount failed transiently
//       · valid     ⇒ loaded
// ★★★★ AND `valid` MEANS **FULLY SEMANTICALLY VALID**, ⛔ NOT MERELY SIZE-AND-HEADER-VALID (spec §3-P1, QA round 2).
//      `mrnv::ui_preset_blob_state` answers the STORAGE question (length, magic, version, backend); it cannot see a
//      slot whose `enabled` byte reads `7`, whose `len` says 40, whose disabled row still carries text, or whose
//      emergency slot is empty. Such a record would be adopted as the wearer's catalog and would then defeat the
//      write-coalescing compare forever. ⇒ `presets_canonical` below is asked at EVERY load and a violation is
//      classified `invalid` — defaults, warned, repairable. ⛔ Corrupted slot fields must never classify as valid.
//
// ★★★ THE CANONICAL RECORD BYTES (owner-ruled — *"coalescing is unreliable without them"*), each one a pin AND a
//     mutation, and each enforced from BOTH SIDES: the one composition path WRITES them, `presets_canonical` REFUSES
//     a record that breaks them.
//       · the generation starts at 1 and SKIPS ZERO on wrap   · `enabled`/`loc` are exactly 0 or 1
//       · the emergency slot has `enabled = 1`                · the bytes AT and AFTER `len` are zero
//       · a disabled slot has zero length, zero text and zero location
//       · padding/reserved bytes are NAMED and zero           (`UiPresetBlob::reserved` / `reserved_tail`)
//       · ★ THE CANDIDATE GENERATION IS SAVED **WITH** THE CANDIDATE, and the live catalog is published ONLY after
//         the save SUCCEEDS. ⛔ Never "save, then increment": a failed save publishes NOTHING (spec §2's corrected
//         transaction row — construct the canonical candidate WITH the next non-zero generation → save → publish).
//
// ★ WHAT IT REUSES RATHER THAN FORKS (U1), verified at the declaration (V1):
//     · the RECORD and its four-state read: `mrnv::UiPresetBlob` / `mrnv::UiPresetRead` (`src/device_nv.h`), which in
//       turn reuse `SlotIo` / `slot_size_ok` / `blob_valid_exact` — one version+length+backend policy for all six;
//     · the whole-record write guard and its `unchanged ⇒ ZERO writes` verdict: `mrfw::JoinProfileService::commit`'s
//       shape and `ProfileVerdict`'s vocabulary;
//     · the printable-byte domain: `mrui::ui_display_byte`'s `0x20 .. 0x7e` — ⛔ not a second opinion about which
//       bytes this panel can show.
//
// ⛔⛔ THE LIMIT OF EVERY CLAIM BELOW, in the words `/mrjoin`, §PROV-TX and K1 all had to use: these properties are
//    proved against a FAKE store that COUNTS calls. ⛔ No NVS/LittleFS write, no flash WEAR and no
//    reset-during-write behaviour is exercised ([[B193]]) — a `save` that reports failure may have written
//    PARTIALLY, so ⛔ nothing here or in its console voice may say "no flash was changed". The `/mrui` power-cut
//    behaviour is METAL-ONLY (M2) and is owed as a bench part.
//
// ⓘ WHAT THIS FILE DELIBERATELY DOES **NOT** HAVE YET (per [[meshroute-mark-done-vs-missing-in-code]]):
//     · ⛔ NO CONSOLE VERB. The `ui preset list/set/clear/reset` grammar, its three NDJSON records and its six
//       reasons are **P2**, and the `busy` table it must obey is spec §2's. This header exposes the TYPED verdicts
//       that verb will map; it prints nothing and parses nothing.
//       ✅ **LANDED 2026-08-25 (P2)** — and this bullet is UPDATED, ⛔ not deleted: the statement about THIS FILE is
//       still true and still load-bearing (it prints nothing and parses nothing). The verb, the grammar, the three
//       records and the six reason spellings live in `src/firmware_ui_preset_verbs.h` (pure, natively pinned,
//       `--target=uipresetverbs`); the `Print` adapter, the ONE `PresetCatalog` instance and the `dispatch` arm are
//       in `src/firmware_commands.cpp`.
//     · ⛔ NO COMPOSE-LIST PROJECTION and ⛔ no `SendReq` change. Rendering the enabled slots in stable-slot order,
//       the `L` marker, the per-frame generation freeze and the `PRESET CHANGED` stale-generation refusal are **P3**.
//       ⇒ `mrui::kDmTexts` / `kChannelTexts` / `kEmergencyText` (`src/firmware_ui_model.h:1370-1379`) are STILL the
//       live compose tables and are deliberately UNTOUCHED by this slice; the duplication of those five strings in
//       `kPresetDefaults` below is TEMPORARY and P3 closes it. ★ It cannot silently DRIFT in the meantime:
//       `test/test_firmware_ui_presets.cpp` asserts the two spellings byte-for-byte against each other.
//       ✅ **LANDED 2026-08-26 (P3)**, and the marker is UPDATED IN PLACE rather than deleted (the
//       [[meshroute-mark-done-vs-missing-in-code]] rule cuts both ways). Every clause above is now discharged, and
//       the DUPLICATION IS GONE: **`mrui::kDmTexts` / `kChannelTexts` / `kEmergencyText` are RETIRED** — their
//       withdrawn declarations are kept visible at their old home in `src/firmware_ui_model.h`, and
//       `kPresetDefaults` below is the ONE place the five compiled strings now live. The panel reads a PROJECTION
//       of the live record (`mrui::compose_project` -> `UiSnapshot::preset_dm` / `preset_ch`, frozen per frame),
//       `mrui::SendReq` carries `{slot, generation}`, and `mrui::send_gate_of` (`src/firmware_ui_send.h`) is the
//       stale-generation refusal. ⓘ The drift fence's case is therefore DISCHARGED with the duplication it fenced;
//       what stands in its place asserts the defaults against the SHIPPED projection instead of against a copy.
//       ⛔ THE PARAGRAPH ABOVE STANDS UNCHANGED IN ONE RESPECT: this file still holds no compose code and no
//       `SendReq` — P3 consumes the RECORD and the slot space, and re-decides nothing here.
//     · ⛔ NO BOOT/STATUS WIRING. `preset_boot_line` below is the exact owner-approved text and the natively-pinned
//       authority for it; the `mrcon.println` that emits it is the boot path's, i.e. P2's, in the TU no gate compiles.
//       ✅ **LANDED 2026-08-25 (P2)**, and the split held exactly as written: the DECISION (call `begin()`, ask
//       `preset_boot_line`, print nothing for `ok`/`absent`, and stop claiming a fault once a successful mutation
//       has repaired the record) is `mrfw::preset_boot_restore` + `mrfw::PresetDiag` in
//       `src/firmware_ui_preset_verbs.h`, driven natively and by `tools/probe_firmware_ui/`; `src/fw_main.cpp` gains
//       ONE CALL (`mrfw::preset_boot_restore_console()`), ⛔ not a decision.
#pragma once
#include <cstdint>
#include <cstring>        // memcmp/memcpy/strlen — the whole-record write guard and the one composition path
#include "device_nv.h"    // mrnv::UiPresetBlob / UiPresetSlot / UiPresetRead — THE durable carrier (U2)

namespace mrfw {

// ---- the durable seam ------------------------------------------------------------------------------------------
// ★ A FOURTH store interface is not a fork of `ICfgStore` / `IJoinStore` / `ITeamKeyStore` (U1 was checked first): a
//   different record, and — like the last two and ⛔ unlike `ICfgStore` — a FOUR-valued read whose whole purpose is
//   to keep absent, corrupt and unreadable apart. Widening any existing seam would change `/mrcfg`'s, `/mrjoin`'s or
//   `/mrteams`' behaviour inside a storage slice (C1).
// ⛔⛔ AND IT IS THE **ONLY** SEAM THIS SERVICE HOLDS, which is how the design's *"a phrase edit can never
//    reprovision the node"* is delivered HERE: there is no `mrnv::Blob`, no `ICfgStore` and no `/mrcfg` writer in
//    scope, so no path through this file can reach the record whose version mismatch reprovisions the device. (The
//    ATTACKABLE half of that pin lives one layer down, in `device_nv.h`'s `kSlotUi` / `kUiPresetMagic`, where a
//    mutation can point this record at `/mrcfg` and a case can redden it.)
struct IUiPresetStore {
    virtual ~IUiPresetStore() = default;
    virtual mrnv::UiPresetRead load(mrnv::UiPresetBlob& out) = 0;
    virtual bool save(const mrnv::UiPresetBlob& b) = 0;   // false = THE WRITE FAILED. ⛔ Never "nothing was written".
};

// ★★★ THE `busy` SEAM, AND IT IS A **QUESTION ASKED AT HANDLING TIME**, ⛔ never a flag this service caches. Spec §2
//     rules that an ACTIVE EMERGENCY makes EVERY mutating verb return `busy`, *including a no-op* — because an
//     alarm's retry series must not have its body or its location policy changed halfway through. A cached copy of
//     that fact is a copy that can be stale at exactly the moment it matters, which is the K3 handling-time re-check
//     discipline applied to an operator's act.
// ⓘ It is `const` and answers a BOOLEAN: the catalog has no business knowing anything else about the emergency.
struct IEmergencyGate {
    virtual ~IEmergencyGate() = default;
    virtual bool emergency_active() const = 0;
};

// ---- the stable slot space -------------------------------------------------------------------------------------
// ★★ THE ARRAY INDEX **IS** THE STABLE SLOT IDENTITY (§3.2.2): 0 = emergency, 1..8 = dm1..dm8, 9..16 =
//    channel1..channel8. The design forbids deriving `dmN` from a compose-list ROW index (§B66's cure), so the id
//    must exist independently of any list — and the cheapest id that cannot drift is the position in the record.
inline constexpr uint8_t kPresetEmergency  = 0;
inline constexpr uint8_t kPresetDmFirst    = 1;
inline constexpr uint8_t kPresetPerKind    = 8;    // eight dm + eight channel (§3.2.2 — a FIXED capacity)
inline constexpr uint8_t kPresetChannelFirst = uint8_t(kPresetDmFirst + kPresetPerKind);   // 9
// ⛔ DERIVED, ⛔ never re-typed: the record's capacity and this index space are the SAME seventeen, and a hand-written
//    17 here is exactly how the two would come to disagree (§B66's own lesson, one record over).
static_assert(mrnv::kUiPresets == 1u + 2u * kPresetPerKind, "firmware_ui_presets.h: the /mrui slot space and the record disagree");

enum class PresetKind : uint8_t {
    emergency,   // the ONE mandatory slot — long-press only, ⛔ never a compose row (§3.2.2)
    dm,          // TEAM -> DM compose
    channel,     // SEND -> channel compose
    // ★★★ THE INVENTORY SENTINEL — the fifth instance of this fence in the tree (`GrantSave::count`,
    //     `mrui::InviteGrantState::count`, `SavedKeyUse::count`, `KeyringForget::count`), added for the reason those
    //     four exist: a HAND-WRITTEN inventory has already failed this arc once. Two independent axes, neither a
    //     literal: (1) the sweep iterates `0 .. count-1`, so an arm added above this line is visited BY
    //     CONSTRUCTION; (2) `preset_kind_name`'s switch has ⛔ NO `default:`, so an arm added and NOT worded is a
    //     BUILD FAILURE under the blanket `-Werror=switch`.
    // ⛔ IT IS ⛔ NOT A KIND: ⛔ no slot may carry it. It must stay LAST — that is what makes it the count.
    count
};
// enum -> string, `default`-LESS so `-Werror=switch` fails the build when an arm is added. Same discipline and the
// same reason as `mrnv::peer_put_name` / `mrfw::profile_err_name` / `keyring_verdict_name`: this project has shipped
// THREE enum->string defects that the byte-identity gate was structurally blind to.
inline const char* preset_kind_name(PresetKind k) {
    switch (k) {
        case PresetKind::emergency: return "emergency";
        case PresetKind::dm:        return "dm";
        case PresetKind::channel:   return "channel";
        // ⛔ THE SENTINEL IS ⛔ NOT A KIND, so it has NO word — spelled out HERE rather than left to a `default:`,
        //    which would swallow a REAL arm added above it (`saved_key_use_name`'s own note).
        case PresetKind::count:     return "?";
    }
    return "?";     // total function; -Werror=switch fires before this is reachable for a valid enumerator
}

// ⛔ `long`, ⛔ not `uint8_t`, and for `valid_profile_slot`'s reason: the caller is a PARSE, and a parse must be able
//    to hand over an out-of-range value for this to REFUSE. Narrowing at the boundary would make `bad_slot`
//    unreachable for exactly the inputs it exists to catch (17, -1, 4000000000).
inline bool preset_slot_valid(long slot) { return slot >= 0 && slot < static_cast<long>(mrnv::kUiPresets); }
// ★ THE ONE MANDATORY SLOT (§3.2.2): *"Emergency cannot be disabled, cleared or made empty."* Named once so `clear`'s
//   refusal and the canonical predicate's requirement can never be two different opinions about which slot it is.
inline bool preset_slot_mandatory(uint8_t slot) { return slot == kPresetEmergency; }
inline PresetKind preset_kind_of(uint8_t slot) {
    if (slot == kPresetEmergency)       return PresetKind::emergency;
    if (slot < kPresetChannelFirst)     return PresetKind::dm;
    return PresetKind::channel;
}
// The operator-facing 1..8 of `dm3` / `channel7`. ⛔ 0 for emergency, which HAS no ordinal — it is one slot, not the
// first of a series, and numbering it would invite an `emergency1`.
inline uint8_t preset_ordinal_of(uint8_t slot) {
    if (slot == kPresetEmergency)   return 0;
    if (slot < kPresetChannelFirst) return uint8_t(slot - kPresetDmFirst + 1);
    return uint8_t(slot - kPresetChannelFirst + 1);
}
// The inverse, for P2's `dm1..dm8` / `channel1..channel8` parse. ⛔ Returns -1 rather than clamping (C2): an ordinal
// this catalog does not have is a REFUSAL, never the nearest slot.
inline int preset_slot_index(PresetKind kind, long ordinal) {
    switch (kind) {
        case PresetKind::emergency: return ordinal == 0 ? int(kPresetEmergency) : -1;
        case PresetKind::dm:
            return (ordinal >= 1 && ordinal <= kPresetPerKind) ? int(kPresetDmFirst + ordinal - 1) : -1;
        case PresetKind::channel:
            return (ordinal >= 1 && ordinal <= kPresetPerKind) ? int(kPresetChannelFirst + ordinal - 1) : -1;
        case PresetKind::count:     return -1;   // ⛔ not a kind — see the sentinel's note
    }
    return -1;
}

// ---- explicit outcomes -----------------------------------------------------------------------------------------
// The vocabulary is `ProfileVerdict`'s, deliberately (U1): the two services answer the same three questions — did it
// apply, did the store already say exactly this, or did it refuse — over the same load-edit-store transaction shape.
// ⛔ THERE IS NO `empty` ARM, AND ITS ABSENCE IS A DECISION, exactly as `KeyringVerdict`'s note argues its own: an
//    ABSENT `/mrui` is not a distinguishable OUTCOME here, because the catalog is ALWAYS live (the compiled defaults
//    are running), so a mutation over an absent store either changes the running catalog (⇒ `ok`, one write) or does
//    not (⇒ `unchanged`, zero writes). An arm with no producer is coverage that is not there — the defect class this
//    codebase has now registered twice.
enum class PresetVerdict : uint8_t {
    ok,          // the verb applied and performed EXACTLY ONE write; the live catalog is PUBLISHED
    unchanged,   // the store already holds exactly this -> ★ ZERO writes, ⛔ no generation change (the wear guard)
    refused,     // a validation / slot / mandatory / busy / unreadable-store refusal: ⛔ ZERO writes, ⛔ no live change
    nv_failed,   // the ONE save attempt failed -> ⛔ NOTHING was published (spec §2: a failed save publishes nothing)
    count        // ⛔ the INVENTORY SENTINEL — see PresetKind::count. ⛔ Not an outcome; must stay LAST.
};
// ★ THE SIX REASONS ARE §3.2.3's `ui_preset_err` SET, and P2 renders them VERBATIM — which is why they are spelled
//   here, in the pure unit, rather than in the verb: an owner re-wording then changes one place and a native case
//   sees it. ⓘ `store` deliberately covers BOTH unreadable states AND a failed save: the six reasons are the design's
//   and this header may not invent a seventh. The DISTINCTION the wearer needs (record-corrupt vs store-dead) is
//   carried by `preset_boot_line` at boot, where it is actionable, ⛔ not smuggled into the verb's reason set.
enum class PresetErr : uint8_t {
    none,
    bad_slot,    // outside 0..16 — the off-by-one ends are both pinned
    bad_text,    // OQ-A's bound (1..17), a non-printable byte, `"` / `\` / CR / LF, or all-spaces
    // ★★★★ **P1 HAS NO PRODUCER FOR THIS ARM, AND THAT IS RECORDED HERE RATHER THAN FIXED** (the standing
    //      [[meshroute-mark-done-vs-missing-in-code]] rule; ADDED 2026-08-25 on QG blocker 3, and the WITHDRAWN
    //      DECISION IS KEPT VISIBLE): this enum omitted `bad_location` on the argument that an arm with no producer
    //      is coverage that is not there — `KeyringVerdict`'s reason for dropping `/mrjoin`'s `empty`. ⛔ THAT WAS
    //      THE WRONG CALL HERE, and the difference is WHOSE vocabulary this is: `KeyringVerdict` is one service's
    //      private outcome type, whereas THIS enum is §3.2.3's **`ui_preset_err` reason set**, a published
    //      companion-facing contract of exactly six reasons. Shipping five and calling it six would have made P2 —
    //      or the iOS app reading `INBOX_SYNC_CONTRACT.md` — invent the sixth spelling for itself.
    //  ⇒ IT IS DECLARED NOW, AND ITS PRODUCER IS **P2's `loc=<on|off>` PARSER**. ⛔ Nothing in this file can return
    //    it and nothing in this file should: the service takes `loc` as a `bool`, so every value it can be handed IS
    //    a valid location. `loc=maybe` is a PARSE failure, and P2 maps it to this arm. ⓘ It is therefore swept by
    //    the `count` fence and worded below (so `-Werror=switch` holds), and ⛔ deliberately has no service-level
    //    case driving a return of it — P2's parse cases will.
    // ✅ **CLOSED 2026-08-25 BY §UI-10/11 P2**, and the marker is UPDATED IN PLACE rather than deleted (the
    //    [[meshroute-mark-done-vs-missing-in-code]] rule cuts both ways — a debt discharged is as much a fact as a
    //    debt outstanding): the producer is `mrfw::preset_parse_loc` in `src/firmware_ui_preset_verbs.h`, which
    //    accepts EXACTLY `loc=on` / `loc=off` and refuses every third spelling — `loc=maybe`, `loc=1`, `loc=ON`,
    //    `loc=`, and an ABSENT location term — to this arm. It is driven by
    //    `test/test_firmware_ui_preset_verbs.cpp` and attacked by `--target=uipresetverbs`. ⛔ The paragraph above
    //    stands unchanged: this file still cannot and must not return it.
    bad_location,
    mandatory,   // `clear emergency` — ⛔ REFUSED, never honoured (§3.2.2)
    busy,        // an ACTIVE EMERGENCY: every mutating verb, ⛔ including a no-op (spec §2)
    store,       // the store is unreadable (`invalid` is repaired instead — see `mutate`), or the ONE save failed
    count        // ⛔ the INVENTORY SENTINEL — see PresetKind::count. ⛔ Not a reason; must stay LAST.
};
inline const char* preset_verdict_name(PresetVerdict v) {
    switch (v) {
        case PresetVerdict::ok:        return "ok";
        case PresetVerdict::unchanged: return "unchanged";
        case PresetVerdict::refused:   return "refused";
        case PresetVerdict::nv_failed: return "nv_failed";
        case PresetVerdict::count:     return "?";     // ⛔ the sentinel is not an outcome
    }
    return "?";
}
inline const char* preset_err_name(PresetErr e) {
    switch (e) {
        case PresetErr::none:      return "none";
        case PresetErr::bad_slot:  return "bad_slot";
        case PresetErr::bad_text:  return "bad_text";
        case PresetErr::bad_location: return "bad_location";   // ⛔ P2's parser produces it; see the enum's note
        case PresetErr::mandatory: return "mandatory";
        case PresetErr::busy:      return "busy";
        case PresetErr::store:     return "store";
        case PresetErr::count:     return "?";         // ⛔ the sentinel is not a reason
    }
    return "?";
}
struct PresetResult {
    PresetVerdict verdict = PresetVerdict::refused;
    PresetErr     err     = PresetErr::none;
};

// ---- the TWO BOOT/STATUS DIAGNOSTICS, EXACT (spec §3-P1, QA round 2) --------------------------------------------
// ★★★ THE OWNER APPROVED THE WORDS, so they are spelled ONCE, in a pure unit, and pinned by a native case — the
//     `kKeyringFullText` precedent (spec string S-30) and for its reason: a lexeme spelled inside
//     `src/fw_main.cpp`'s boot path is a lexeme NO gate compiles (§B115), so a re-wording could silently diverge from
//     the spec. ⓘ Each says what happened AND what the wearer can do about it, which is the difference between the
//     two states: an INVALID record repairs itself on his next successful change; an UNREADABLE store accepts no
//     changes at all until the device is dealt with.
inline constexpr const char* kPresetInvalidLine =
    "  ui presets = DEFAULTS (record invalid — repaired on next successful change)";
inline constexpr const char* kPresetIoFailedLine =
    "  ui presets = DEFAULTS (store unreadable — changes disabled)";
// ★★ `nullptr` IS THE THIRD ANSWER AND IT IS THE RULING: *"a valid or absent store prints NO presets line"*. An
//    ordinary first boot is not a fault and must not read like one — the whole reason `absent` and `invalid` are
//    different states. ⛔ Never an empty string: a caller that printed one would emit a blank line at boot.
inline const char* preset_boot_line(mrnv::UiPresetRead st) {
    switch (st) {
        case mrnv::UiPresetRead::ok:        return nullptr;   // loaded — say nothing
        case mrnv::UiPresetRead::absent:    return nullptr;   // ★ a first boot is SILENT (owner-ruled)
        case mrnv::UiPresetRead::invalid:   return kPresetInvalidLine;
        case mrnv::UiPresetRead::io_failed: return kPresetIoFailedLine;
    }
    return nullptr;
}

// ---- validation (§3.2.2's text rule, with OQ-A's bound) ---------------------------------------------------------
// ★★★ 1..17 PRINTABLE ASCII, and the 17 is OQ-A's OWNER RULING of 2026-08-25: the compose row ALWAYS shows a
//     selection marker AND a location marker (`L` or `-`), so BOTH location states consume 2 of the panel's 19
//     columns. ⛔ The draft's conditional bound — 18 when `loc=off` — was WRONG; it is kept visible in the spec. The
//     device must never send a hidden suffix the wearer could not inspect, so 18+ bytes REFUSE (C2), ⛔ never truncate.
// ★ THE PRINTABLE DOMAIN IS `mrui::ui_display_byte`'s (`0x20 .. 0x7e`), ⛔ not a second opinion about which bytes
//   this panel can show. ⓘ CR and LF are excluded BY THAT RANGE, and this is said rather than re-checked: an explicit
//   `c == '\r'` test after it would be unreachable, so no mutation could redden it and the entry would report a
//   measurement that is not there (the `crypto_wipe -> memset` lesson, K1's own).
// ⛔ `"` AND `\` ARE REFUSED SEPARATELY because they ARE printable: they are the two bytes that would break the
//    quoted console/NDJSON forms P2 emits, so a phrase carrying one is refused at the SOURCE rather than escaped at
//    each of the three renderers.
// ★ ≥ 1 NON-SPACE: `"   "` is a phrase that renders as an empty row, i.e. a slot the wearer believes is configured
//   and cannot see. §3.2.2 requires at least one non-space character.
// ★★★★ `size_t`, ⛔ **NEVER `uint8_t`** — QG blocker 1, 2026-08-25, and the WITHDRAWN SIGNATURE IS KEPT VISIBLE
//      BECAUSE IT WAS A LIVE BYPASS: this took `uint8_t len`, so a caller's 273-byte phrase NARROWED AT THE CALL to
//      273 & 0xFF = **17** — the exact bound — and was ACCEPTED as valid instead of refused as `bad_text`. The
//      validator never saw the real length, so no amount of checking inside it could have caught the input. ⇒ THE
//      WIDE TYPE IS THE FIX, and it must reach the PUBLIC BOUNDARY (`PresetCatalog::set`) or the narrowing simply
//      moves one frame outwards. ⛔ THE ORDER IS THE OTHER HALF: the bound is tested on the WIDE value, and the
//      narrow happens only afterwards, inside `preset_slot_put`, where 17 is already proven.
// ⓘ It is the [[B216]] family seen from the type system's side: a predicate that is the NEGATION of a reject
//   condition is only as sound as the value it is handed.
inline PresetErr validate_preset_text(const char* text, size_t len) {
    if (!text) return PresetErr::bad_text;                            // C2 — ⛔ never "an empty phrase"
    // ★ TESTED ON THE WIDE VALUE, BEFORE ANY NARROWING — see the block above. 273 must fail here, not become 17.
    if (len == 0 || len > mrnv::kUiPresetTextMax) return PresetErr::bad_text;
    bool non_space = false;
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x20 || c > 0x7e) return PresetErr::bad_text;         // ⇒ NUL, CR, LF, DEL and every high byte
        if (c == '"' || c == '\\') return PresetErr::bad_text;
        if (c != ' ') non_space = true;
    }
    return non_space ? PresetErr::none : PresetErr::bad_text;
}

// ---- the generation (§3.2.3) ------------------------------------------------------------------------------------
// ★★★ IT SKIPS ZERO ON WRAP, AND THAT IS THE WHOLE POINT OF THE FUNCTION. `0` is reserved for "no generation", so a
//     catalog that wrapped onto it would be indistinguishable from an unstamped one — and P3's `SendReq` seals the
//     generation the wearer SAW, so a zero would make the stale-generation refusal unable to fire. ⓘ Consumers
//     compare for EQUALITY, never ordering (§3.2.3), which is what makes uint32 wrap harmless in the first place.
// ⓘ The FIRST generation is `mrnv::ui_preset_blob_init`'s 1 and is spelled only there (U1).
inline uint32_t preset_generation_next(uint32_t g) {
    const uint32_t n = g + 1u;
    return n == 0u ? 1u : n;
}

// ---- the COMPILED DEFAULTS — §3.2.2's table, VERBATIM ------------------------------------------------------------
// ★ INDEX-ALIGNED WITH THE RECORD's `slot[]`, so the table IS the stable-slot map and there is no second ordering to
//   keep in step. `text == nullptr` means the slot's compiled state is DISABLED AND EMPTY — slots `dm3..dm8` and
//   `channel3..channel8`, exactly as the design's table says.
// ⛔⛔ ✅ **CLOSED 2026-08-26 BY P3 — THE DUPLICATION IS GONE AND THESE FIVE STRINGS ARE NOW THE ONLY COPY.** The
//    paragraph below is KEPT VISIBLE as the reasoning that made the duplication acceptable for exactly one slice;
//    ⛔ its PRESENT TENSE is now false (corrected on QG's sweep): there are no `mrui::kDmTexts` / `kChannelTexts` /
//    `kEmergencyText` and no `firmware_ui_model.h:1370-1379`. P3 retired both tables and the emergency constant,
//    the model consumes THIS catalog through `mrui::compose_project`, and the drift-fence case that guarded the two
//    spellings against each other is DISCHARGED with what it fenced. ⇒ an edit HERE now changes what the panel
//    shows, with no second table to keep in step.
// ⛔⛔ THE FIVE STRINGS ARE DUPLICATED FROM `mrui::kDmTexts` / `kChannelTexts` / `kEmergencyText`
//    (`src/firmware_ui_model.h:1370-1379`) AND THAT DUPLICATION IS **TEMPORARY, DELIBERATE AND BOUNDED**, ⛔ not the
//    U1 rot: those tables are the LIVE compose lists until P3 makes the model consume this catalog, and this slice
//    may not touch them (C1 — a UI change inside a storage slice). Merging by INCLUDE is not available either: this
//    header must stay model-INCLUDABLE (P3 needs it from inside `firmware_ui_model.h`), and a header the model
//    includes may not include the model — the §UI-16 N2 rule. ★ THE DRIFT IS FENCED WHERE IT CAN BE: the battery's
//    suite asserts these five spellings byte-for-byte against the model's, so the two cannot diverge while both live.
struct PresetDefaultRow {
    const char* text;   // nullptr = the compiled state is DISABLED and EMPTY
    uint8_t     loc;    // exactly 0/1 — the canonical byte, ⛔ never a "non-zero is true"
};
inline constexpr PresetDefaultRow kPresetDefaults[mrnv::kUiPresets] = {
    { "I'm in danger", 1 },        //  0 emergency — ★ location ON, enabled, MANDATORY (§3.2.2 row 1)
    { "Are you OK?",   0 },        //  1 dm1
    { "I'm OK",        0 },        //  2 dm2
    { nullptr,         0 },        //  3 dm3   \.
    { nullptr,         0 },        //  4 dm4    |
    { nullptr,         0 },        //  5 dm5    |  §3.2.2: "empty, location off / disabled"
    { nullptr,         0 },        //  6 dm6    |
    { nullptr,         0 },        //  7 dm7    |
    { nullptr,         0 },        //  8 dm8   /
    { "Got your message", 0 },     //  9 channel1
    { "All good",         0 },     // 10 channel2
    { nullptr,         0 },        // 11 channel3  \.
    { nullptr,         0 },        // 12 channel4   |
    { nullptr,         0 },        // 13 channel5   |  §3.2.2: "empty, location off / disabled"
    { nullptr,         0 },        // 14 channel6   |
    { nullptr,         0 },        // 15 channel7   |
    { nullptr,         0 },        // 16 channel8  /
};

// ---- the ONE composition path (U2 — ⛔ never rebuild the carrier field-by-field at a second site) ----------------
// ★★★ THE SLOT IS ZEROED FIRST, WHOLE, and that is not tidiness — it is THREE of the owner's canonical-byte rules
//     discharged by one statement: the bytes after `len` are zero, a disabled slot has zero text/length/location,
//     and every byte is DETERMINISTIC so the whole-record compare that IS the write-coalescing policy answers the
//     same way on the same catalog. (The reasoning `join_profile_put` and `team_key_rec_put` both state.)
// ★ `enabled` / `loc` ARE STAMPED AS EXACTLY 0 OR 1 — the ternaries below are the canonical-byte rule in code, ⛔ not
//   a cast of whatever the caller's bool happened to be represented as.
// ⛔ CALL ONLY AFTER `validate_preset_text` RETURNED `none` for an ENABLED slot: this function does not re-validate,
//    it CANONICALISES.
// ★★★ IT TAKES `size_t` TOO, AND THIS IS **THE ONE PLACE THE NARROW HAPPENS** (QG blocker 1): the clamp below is the
//     last fence in front of a `memcpy` bound — `join_profile_put`'s reason, a memcpy bound is not the place to trust
//     a caller — and it CLAMPS a wide value that the validator has already refused. ⛔ Were the parameter `uint8_t`,
//     273 would arrive here as 17 and the clamp would have nothing left to see.
inline void preset_slot_put(mrnv::UiPresetSlot& s, bool enabled, bool loc, const char* text, size_t len) {
    s = mrnv::UiPresetSlot{};                       // ★ zeroes text, len, loc and enabled — see the block above
    if (!enabled) return;                           // ★ a disabled slot IS the zeroed slot, by construction
    s.enabled = 1;
    s.loc     = loc ? 1 : 0;
    if (len > mrnv::kUiPresetTextMax) len = mrnv::kUiPresetTextMax;   // validate_preset_text already refused this
    if (text && len) { memcpy(s.text, text, len); s.len = static_cast<uint8_t>(len); }   // ★ narrowed AFTER the clamp
}
// The defaults table -> ONE canonical slot. ⛔ Spelled once so `reset <slot>` and `reset all` cannot come to disagree
// about what a compiled default IS.
inline void preset_slot_default(mrnv::UiPresetSlot& s, uint8_t slot) {
    const PresetDefaultRow& d = kPresetDefaults[slot];
    const size_t len = d.text ? strlen(d.text) : 0;   // ★ `size_t` all the way, for `validate_preset_text`'s reason
    preset_slot_put(s, d.text != nullptr, d.loc != 0, d.text, len);
}
// The COMPLETE compiled catalog, as a canonical record: header stamped, generation 1, seventeen canonical slots.
// ★ This is what an ABSENT store MEANS, what `reset all` restores, and what the live catalog falls back to in all
//   three non-`ok` states. ONE definition, so the three can never be three different catalogs.
inline void preset_defaults(mrnv::UiPresetBlob& b) {
    mrnv::ui_preset_blob_init(b);                   // magic, version, generation 1, every other byte zero (U2)
    for (uint8_t i = 0; i < mrnv::kUiPresets; ++i) preset_slot_default(b.slot[i], i);
}

// ---- the CANONICAL PREDICATE — the other half of every canonical-byte rule (spec §3-P1, QA round 2) --------------
// ★★★★ WHY A RECORD THAT PASSED `mrnv::ui_preset_blob_state` IS STILL ASKED THIS: that predicate judges the
//      STORAGE — length, magic, version, backend. It cannot see a slot whose `enabled` byte reads `7`, whose `len`
//      says 40, whose DISABLED row still carries the phrase the wearer deleted, or whose EMERGENCY slot is empty.
//      Adopting such a record would (a) show the wearer a catalog he did not configure while calling it valid, (b)
//      defeat the byte-identical write guard forever (his next identical `set` would compose the canonical form,
//      compare unequal to the corrupt one, and write), and (c) — the `enabled=0` emergency — take away the one slot
//      the design says can never be disabled. ⇒ ⛔ CORRUPTED SLOT FIELDS MUST NEVER CLASSIFY AS VALID.
// ★ IT IS A PREDICATE, ⛔ NOT A REPAIR. Nothing here rewrites anything: a violation classifies the record `invalid`,
//   which means defaults + a counted warning + repair on the wearer's next successful change. Repairing on a READ is
//   exactly the silent fallback this record exists to avoid (`JoinProfileService::list`'s rule).
inline bool preset_slot_canonical(const mrnv::UiPresetSlot& s, bool mandatory) {
    if (s.enabled > 1 || s.loc > 1) return false;                       // ★ EXACTLY 0 or 1
    if (mandatory && s.enabled != 1) return false;                      // ★ the emergency slot is ALWAYS enabled
    if (s.enabled == 0) {
        // ★ A DISABLED SLOT IS ALL ZEROES: no length, no location, no text. ⓘ `loc` is covered by the whole-tail
        //   sweep below only if it lived in `text`; it does not, so it is asked for explicitly.
        if (s.len != 0 || s.loc != 0) return false;
        for (uint8_t i = 0; i < sizeof s.text; ++i) if (s.text[i] != 0) return false;
        return true;
    }
    if (validate_preset_text(s.text, s.len) != PresetErr::none) return false;   // ⛔ ONE authority (U1)
    // ★ THE TAIL: every byte AT and AFTER `len` is zero. ⓘ This is what makes `text[18]` "17 + the canonical
    //   terminator" rather than "17 and whatever follows", and it is the rule that makes the whole-record compare
    //   trustworthy — garbage in the tail is invisible on the panel and fatal to coalescing.
    for (uint8_t i = s.len; i < sizeof s.text; ++i) if (s.text[i] != 0) return false;
    return true;
}
inline bool presets_canonical(const mrnv::UiPresetBlob& b) {
    if (b.generation == 0) return false;                                // ★ NON-ZERO by ruling (see the wrap note)
    if (b.reserved != 0) return false;                                  // ★ NAMED padding, and it is ZERO
    for (uint8_t i = 0; i < sizeof b.reserved_tail; ++i) if (b.reserved_tail[i] != 0) return false;
    for (uint8_t i = 0; i < mrnv::kUiPresets; ++i)
        if (!preset_slot_canonical(b.slot[i], preset_slot_mandatory(i))) return false;
    return true;
}

// ---- the two UNREADABLE answers are ⛔ NOT the same answer here ---------------------------------------------------
// ⚠⚠ AND THIS IS WHERE `/mrui` PARTS FROM `/mrjoin` AND `/mrteams`, DELIBERATELY: both of those name a single
//    `*_read_unreadable` predicate because their verbs refuse on BOTH arms. ★ THIS RECORD'S VERBS DO NOT — the owner
//    ruled `invalid` REPAIRABLE (a successful mutation rewrites the complete canonical catalog) and `io_failed`
//    WRITE-REFUSING. ⇒ a shared "unreadable" predicate here would be the exact collapse the four states exist to
//    prevent, so the two arms are asked SEPARATELY at the one place that cares (`mutate`), and no such helper is
//    declared. ⓘ Stated rather than left as a gap: a reader arriving from `firmware_join_profiles.h` will look for it.
inline bool preset_read_refuses_writes(mrnv::UiPresetRead st) { return st == mrnv::UiPresetRead::io_failed; }

// ---- the service -------------------------------------------------------------------------------------------
// ★★★★ THE STACK GATE, OWNER-RULED (spec §5), AND THE ANSWER IS **RESIDENT MEMBERS — ⛔ ZERO BYTES OF CATALOG ON ANY
//      STACK, ON ANY PATH.** The measurement and the trade, so the decision is visible rather than inherited:
//        · `sizeof(mrnv::UiPresetBlob)` = 372 B. A transaction needs TWO scratch records — the CANDIDATE it composes
//          and the record it READ (the byte-identical compare is between exactly those two) — plus the LIVE catalog,
//          which is resident by requirement because the panel reads it every frame.
//        · AS STACK LOCALS that would be 744 B in the mutating frame. ⛔ REFUSED: `begin()` runs from `setup()`, i.e.
//          on the nRF52 Arduino loop task, whose stack is a FIXED 4 KB (`LOOP_STACK_SZ = 256*4`, not overridable) —
//          744 B is 18 % of the WHOLE stack, and this tree has already HARDFAULTED on that stack once, with
//          `status stackhw=` down to 72 B (`fw_main.cpp` §stability; the `do_post_ack` frame). The console/BLE path
//          runs in the 8 KB `g_mesh_task` whose deepest RX nesting is ~1.4 KB, where 744 B would in fact fit — but a
//          buffer that is safe on ONE of the two entry paths is a buffer waiting for the other one.
//        · ⇒ THREE MEMBERS, 3 × 372 = 1116 B RESIDENT, and the tree's own precedent is exactly this trade made
//          twice for exactly this reason: `static mrnv::PeerBlob s_peers` (1160 B, `firmware_commands.cpp:45`) and
//          `static Blob cur` in `mrnv::save` both chose resident-over-stack, both citing the `do_post_ack` overflow.
//        · ⛔ AND THE OPPOSITE CHOICE — K1's *"296 B ON THE STACK and deliberately NOT static"* — DOES NOT APPLY AND
//          IS NOT A CONTRADICTION: that argument is about a SECRET (a resident copy outlives the call in `.bss`
//          where nothing wipes it). A preset catalog carries no secret; it carries the wearer's phrases, which are
//          displayed on a screen. ⇒ the `s_peers` precedent governs, not the keyring's.
// ⛔ THE TWO SCRATCH MEMBERS ARE **SCRATCH, ⛔ NOT STATE**: each is written WHOLE at the head of the transaction that
//    uses it and is never read across calls, so there is nothing between calls to go stale. That is stated because
//    `JoinProfileService` and `TeamKeyringService` both advertise "no member holding a record between calls", and
//    this service's shape differs for a reason the panel imposes, not by drift.
class PresetCatalog {
  public:
    PresetCatalog(IUiPresetStore& store, const IEmergencyGate& gate) : _store(store), _gate(gate) {
        preset_defaults(_live);   // ★ the catalog is LIVE from construction — ⛔ never an unusable pre-`begin()` state
    }

    // ---- the BOOT LOAD. ⛔ ZERO WRITES ON EVERY PATH, including a corrupt record: repairing on a read is the silent
    //      fallback this record exists to avoid, and the repair is a MUTATION's (see `commit`).
    // ★ The returned state IS the diagnostic: the caller prints `preset_boot_line(st)`, which is `nullptr` for the
    //   two states that are not faults. ⛔ The service does not print; it has no `Print` and cannot acquire one.
    mrnv::UiPresetRead begin() {
        const mrnv::UiPresetRead st = read_store();
        if (st == mrnv::UiPresetRead::ok) { _live = _cur; return st; }
        // ★ ALL THREE non-`ok` states run the COMPILED DEFAULTS — the difference between them is what the wearer is
        //   TOLD (nothing / invalid / unreadable) and what a later mutation may DO, ⛔ never what he sees on the panel.
        preset_defaults(_live);
        return st;
    }

    // ---- reads (P2's `list`, P3's renderer) ----------------------------------------------------------------------
    const mrnv::UiPresetBlob& live() const { return _live; }
    uint32_t generation() const { return _live.generation; }
    // ⛔ CLAMPED, ⛔ never UB: an out-of-range index answers the emergency slot rather than reading past the array.
    //    ⓘ `preset_slot_valid` is the REFUSAL a verb owes its operator; this is the last fence in front of an access.
    const mrnv::UiPresetSlot& slot(uint8_t i) const {
        return _live.slot[i < mrnv::kUiPresets ? i : kPresetEmergency];
    }
    // How many slots of one kind are ENABLED — `ui_presets_end`'s `dm_active` / `channel_active` (§3.2.3), and P3's
    // "zero enabled ⇒ the empty state" test. ⛔ Emergency is not counted into either: it is never a compose row.
    uint8_t enabled_count(PresetKind kind) const {
        uint8_t n = 0;
        for (uint8_t i = 0; i < mrnv::kUiPresets; ++i)
            if (_live.slot[i].enabled && preset_kind_of(i) == kind) ++n;
        return n;
    }
    // ★ THE WARNING IS **COUNTED** (spec §3-P1), and the two counters are SEPARATE because the ruling calls the
    //   io_failed warning DISTINCT. A count is a CONSEQUENCE a case can measure; "it warned" is a claim.
    uint16_t invalid_loads()   const { return _invalid_loads; }
    uint16_t io_failed_loads() const { return _io_failed_loads; }
    uint16_t saves()           const { return _saves; }    // ★ the flash-wear guard, measured rather than argued

    // ---- the mutating verbs. ★★★ THE ORDER IS THE CONTRACT, and it is the same order in all four:
    //        busy -> validate -> load -> compose ONE canonical candidate -> compare -> stamp the NEXT generation
    //        INTO the candidate -> AT MOST ONE save -> publish.
    //      An active emergency costs ⛔ ZERO loads and ZERO writes; a bad slot or a bad phrase costs ZERO loads and
    //      ZERO writes; an `io_failed` store costs ZERO writes; an identical change costs ⛔ ZERO writes.
    // `ui preset set <slot> loc=<on|off> "<text>"` — validates the full record and ENABLES that slot (§3.2.3).
    // ★★★★ `size_t len`, ⛔ **NEVER `uint8_t`** — QG blocker 1, and THIS is the boundary that matters: a narrowing
    //      here happens at the CALL, before one line of this service runs, so a 273-byte phrase would arrive as 17
    //      and every check below would agree it was fine. The withdrawn signature is recorded at
    //      `validate_preset_text`, with the arithmetic. ⛔ Do not "tidy" this back to the record's own width.
    PresetResult set(long slot, bool loc, const char* text, size_t len) {
        PresetResult r{};
        if (_gate.emergency_active()) { r.err = PresetErr::busy; return r; }        // ⛔ 0 loads, 0 writes
        if (!preset_slot_valid(slot)) { r.err = PresetErr::bad_slot; return r; }    // ⛔ 0 loads, 0 writes
        const PresetErr ve = validate_preset_text(text, len);
        if (ve != PresetErr::none) { r.err = ve; return r; }                        // ⛔ 0 loads, 0 writes
        const mrnv::UiPresetRead st = read_store();
        if (preset_read_refuses_writes(st)) { r.err = PresetErr::store; return r; } // ⛔ 0 writes
        _cand = _live;
        preset_slot_put(_cand.slot[slot], /*enabled=*/true, loc, text, len);
        return commit(st);
    }

    // `ui preset clear <dm1..dm8|channel1..channel8>` — DISABLES the slot and clears its body and location flag.
    // ⛔⛔ `clear emergency` FAILS WITH `mandatory` — the design's flat rule (*"Emergency cannot be disabled, cleared
    //    or made empty"*), refused BEFORE any load and with ⛔ zero writes. It is the sharpest invariant in the slice:
    //    the emergency phrase is what a long press sends when the wearer is in trouble.
    PresetResult clear(long slot) {
        PresetResult r{};
        if (_gate.emergency_active()) { r.err = PresetErr::busy; return r; }        // ⛔ 0 loads, 0 writes
        if (!preset_slot_valid(slot)) { r.err = PresetErr::bad_slot; return r; }    // ⛔ 0 loads, 0 writes
        if (preset_slot_mandatory(static_cast<uint8_t>(slot))) { r.err = PresetErr::mandatory; return r; }
        const mrnv::UiPresetRead st = read_store();
        if (preset_read_refuses_writes(st)) { r.err = PresetErr::store; return r; } // ⛔ 0 writes
        _cand = _live;
        preset_slot_put(_cand.slot[slot], /*enabled=*/false, /*loc=*/false, nullptr, 0);
        return commit(st);
    }

    // `ui preset reset <slot>` — restore ONE slot's compiled default. ★ For `dm3..dm8` / `channel3..channel8` that
    // means returning to DISABLED (§3.2.3); for `emergency` it restores the compiled phrase and its location flag,
    // which is a text edit and ⛔ not a clear — so it is ALLOWED where `clear` is refused.
    PresetResult reset_slot(long slot) {
        PresetResult r{};
        if (_gate.emergency_active()) { r.err = PresetErr::busy; return r; }        // ⛔ 0 loads, 0 writes
        if (!preset_slot_valid(slot)) { r.err = PresetErr::bad_slot; return r; }    // ⛔ 0 loads, 0 writes
        const mrnv::UiPresetRead st = read_store();
        if (preset_read_refuses_writes(st)) { r.err = PresetErr::store; return r; } // ⛔ 0 writes
        _cand = _live;
        preset_slot_default(_cand.slot[slot], static_cast<uint8_t>(slot));
        return commit(st);
    }

    // `ui preset reset all` — restore the COMPLETE compiled catalog (§3.2.3). ⓘ OQ-B rules that a successful DURABLE
    // `reset all` closes an open selection-phase compose exactly as any other successful mutation does; that half is
    // P3's, and it keys off this verdict (`ok`), ⛔ never off the verb's name.
    PresetResult reset_all() {
        PresetResult r{};
        if (_gate.emergency_active()) { r.err = PresetErr::busy; return r; }        // ⛔ 0 loads, 0 writes
        const mrnv::UiPresetRead st = read_store();
        if (preset_read_refuses_writes(st)) { r.err = PresetErr::store; return r; } // ⛔ 0 writes
        preset_defaults(_cand);
        // ★ THE GENERATION IS THE LIVE ONE, ⛔ not `preset_defaults`' 1: a reset restores the wearer's PHRASES, and
        //   resetting the generation would make a `SendReq` sealed under an OLD catalog compare EQUAL to the new one
        //   — the stale-generation refusal silently disarmed by a verb that is supposed to invalidate everything.
        _cand.generation = _live.generation;
        return commit(st);
    }

  private:
    // ★ THE ONE LOAD PATH (U1), so the semantic classification and the two counters cannot exist in four copies.
    //   ⛔ `_cur` IS RE-INITIALISED on a non-`ok` answer rather than trusted: a failed read may have deposited a
    //   PARTIAL record in it (device_nv.h's §nv-ritual warning).
    mrnv::UiPresetRead read_store() {
        mrnv::UiPresetRead st = _store.load(_cur);
        // ★★★★ THE SEMANTIC GATE (spec §3-P1, QA round 2). A record the STORAGE accepted is still `invalid` if the
        //      CATALOG inside it violates any canonical-byte rule — see `presets_canonical`. ⛔ A corrupted slot
        //      field must never classify as valid.
        if (st == mrnv::UiPresetRead::ok && !presets_canonical(_cur)) st = mrnv::UiPresetRead::invalid;
        // ★★★★ `_cur` IS **WHAT THE DURABLE SIDE HOLDS**, ON EVERY STATE — and making that true of the ABSENT arm is
        //      QG blocker 2's fix. ⛔ An absent record does NOT mean "whatever is running": IT MEANS THE COMPILED
        //      DEFAULTS, because that is what the next boot will produce. Materialising them here is what lets
        //      `commit`'s compare ask ONE question in all three readable states — *would this write change what a
        //      reboot restores?* — instead of two questions, one of which was about RAM.
        // ⛔ AND `ui_preset_blob_init` IS THE WRONG ANSWER FOR `absent`, which is the shape the defect had: an EMPTY
        //    record is not what an absent store restores, and comparing against one would make every no-op over a
        //    fresh device write.
        if (st == mrnv::UiPresetRead::absent) preset_defaults(_cur);
        // ⓘ invalid / io_failed: a failed read may have deposited a PARTIAL record (device_nv.h's §nv-ritual
        //   warning), so `_cur` is re-initialised rather than trusted. It is never COMPARED against on these two
        //   arms — `invalid` is deliberately baseline-less (see `commit`) and `io_failed` never reaches `commit` —
        //   so ⛔ no mutation can redden this line; it is defence in depth for the next reader (P2's `list`, or a
        //   future arm), and "unreachable today" is how a partial record eventually reaches a caller.
        else if (st != mrnv::UiPresetRead::ok) mrnv::ui_preset_blob_init(_cur);
        // ★ COUNTED HERE, AT THE ONE CLASSIFICATION, so every load — the boot one and every mutation's — is counted
        //   by the same statement. Two counters, because the ruling calls the `io_failed` warning DISTINCT.
        if (st == mrnv::UiPresetRead::invalid)   ++_invalid_loads;
        if (st == mrnv::UiPresetRead::io_failed) ++_io_failed_loads;
        return st;
    }

    // ★★★ THE ONE WRITE DECISION, SPELLED ONCE (U1), and every clause of spec §2's corrected transaction row is a
    //     line of it. `_cand` arrives CANONICAL and carrying the CURRENT generation — that is what makes the compare
    //     below a question about the wearer's CHANGE rather than about the counter.
    PresetResult commit(mrnv::UiPresetRead st) {
        PresetResult r{};
        // ★★★★ THE COALESCING BASELINE IS **THE DURABLE SIDE, NEVER THE RUNNING CATALOG** — corrected 2026-08-25 (QG
        //      blocker 2), and the WITHDRAWN SHAPE IS KEPT VISIBLE BECAUSE IT LOST DATA: the `absent` arm used to
        //      compare the candidate against `_live`, i.e. against RAM. ⇒ a node running a CUSTOM catalog whose
        //      `/mrui` had gone ABSENT (a failed or partial storage operation, a formatted FS) answered `unchanged`
        //      to the wearer RE-ENTERING HIS OWN SETTING, wrote nothing, and lost the whole custom catalog at the
        //      next boot — while telling him it was already stored. ⛔ A zero-write answer is only honest when the
        //      DURABLE side already matches; RAM matching proves nothing about what a reboot restores.
        // ★ ⇒ `_cur` HOLDS THE DURABLE SIDE IN BOTH READABLE STATES (see `read_store`), so both arms ask the ONE
        //   honest question — *would this write change what a reboot restores?*:
        //      · ok      -> the record just read;
        //      · absent  -> the COMPILED DEFAULTS, because that is what an absent record restores. ★ The owner's
        //                   pin-8 absent arm is preserved exactly: on a fresh device the running catalog IS the
        //                   defaults, so a `set` that re-states one still costs ⛔ zero writes;
        //      · invalid -> ⛔ NO BASELINE AT ALL. Its bytes are meaningless, so an accidental match would report
        //                   `unchanged` and LEAVE THE RECORD CORRUPT — the one path on which a zero-write answer is
        //                   the dishonest one. ⇒ the compare is SKIPPED and the REPAIR writes the complete canonical
        //                   catalog, exactly as the ruling allows *"even when its live values equal defaults"*.
        //                   (`JoinProfileService::reset`'s `commit_forced` arm, arrived at from the same direction.)
        // ⛔ THE TWO ARMS STAY SEPARATE STATEMENTS rather than one `st != invalid`: each is a ruled decision and each
        //    must be attackable ON ITS OWN (`--target=uipresets` U15/U16), which one merged predicate would prevent.
        if (st == mrnv::UiPresetRead::ok &&
            memcmp(&_cand, &_cur, sizeof _cand) == 0) { r.verdict = PresetVerdict::unchanged; return r; }
        if (st == mrnv::UiPresetRead::absent &&
            memcmp(&_cand, &_cur, sizeof _cand) == 0) { r.verdict = PresetVerdict::unchanged; return r; }
        // ★★★ THE NEXT GENERATION GOES **INTO THE CANDIDATE, BEFORE THE SAVE** (spec §2, corrected 2026-08-25):
        //     construct the canonical candidate WITH the next non-zero generation -> save -> publish. ⛔ Never
        //     "save, then increment": that order persists a record whose generation is already stale, and a power
        //     cut between the two would leave flash holding a catalog the panel calls by another number.
        _cand.generation = preset_generation_next(_live.generation);
        // ⛔ THE ONE SAVE. A failure PUBLISHES NOTHING — the live catalog, the generation and the panel are exactly
        //    as they were, and the verdict says the write did not complete. ⛔ Never "nothing was written": [[B193]]
        //    — a backend can fail AFTER a partial write, which is why the metal power-cut check is owed (M2).
        if (!_store.save(_cand)) { r.verdict = PresetVerdict::nv_failed; r.err = PresetErr::store; return r; }
        ++_saves;
        // ★★★★ THE PUBLISH, AND IT IS **AFTER** THE SAVE — the owner's ruled order and the headline mutation of this
        //      slice. Publishing first would make a node whose flash write failed show, and SEND, phrases that will
        //      vanish at the next boot: [[B240]]'s exact shape (works at the trailhead, dead at the summit) arriving
        //      through a phrase editor.
        _live = _cand;
        r.verdict = PresetVerdict::ok;
        return r;
    }

    IUiPresetStore&       _store;
    const IEmergencyGate& _gate;
    // ★ THE LIVE CATALOG — resident by requirement (the panel reads it every frame) and ALWAYS canonical: it is
    //   either `preset_defaults` or a record that passed `presets_canonical`.
    mrnv::UiPresetBlob _live{};
    // ★ THE TWO SCRATCH RECORDS — see the STACK GATE block above. ⛔ Scratch, ⛔ not state: `_cur` is written whole by
    //   `read_store` and `_cand` whole by its transaction, before either is read.
    mrnv::UiPresetBlob _cur{};
    mrnv::UiPresetBlob _cand{};
    uint16_t _invalid_loads   = 0;
    uint16_t _io_failed_loads = 0;
    uint16_t _saves           = 0;
};

}  // namespace mrfw
