// MeshRoute — src/firmware_config_service.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-13 — THE TYPED STAGED-CONFIGURATION SERVICE (spec docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md
// §3.6.1). ★ HEADLESS BY DESIGN **IN THE SLICE THAT ADDED THIS FILE**: it is an API plus its native tests, with NO
// panel, NO screen and NO cycle change of its own — that separation is why it stayed transport- and render-agnostic.
// ⛔ CORRECTED IN PLACE 2026-08-13 (QG round 2): the sentence continued *"§UI-14's SETTINGS renderer is the first
// consumer, and it is deliberately not here"* — future tense, and a reader who stopped at line 5 was told this
// service is not used. **§UI-14 HAS LANDED and IS that consumer**: `src/firmware_ui.cpp` renders the SETTINGS screen
// and constructs the ONE `ConfigService`, over the device bindings in `src/firmware_config.cpp`. The file is still
// headless — that is a property of the FILE, not of the feature.
//
// ★★★ THERE ARE THREE STATES, NOT TWO, AND EVERY PREDICATE BELOW NAMES WHICH TWO OF THE THREE IT COMPARES
//     (§3.6.1's own heading: "persisted, effective and draft are three different states"):
//       PERSISTED  — what `/mrcfg` holds (`mrnv::Blob`, src/device_nv.h). Survives reboot.
//       EFFECTIVE  — what the RUNNING node uses: `NodeConfig` for the live-class fields, `g_ble_mode` for the
//                    reboot-class one. It legitimately DIFFERS from persisted between a save and the reboot.
//       DRAFT      — the RAM candidate the editor mutates. §3.6.1 calls it the `ConfigDraft`; it is realised here as
//                    `ConfigService::_draft` (a `CfgValues`), so a reader grepping the spec's noun lands here.
//     ⇒ the three predicates are DISTINCT COMPARISONS and must never be collapsed into one boolean:
//       `config_unsaved()`   = DRAFT     vs BASELINE (the recorded persisted snapshot)
//       `conflict()`         = PERSISTED vs BASELINE (somebody else wrote `/mrcfg` under us)
//       `reboot_required()`  = BASELINE  vs EFFECTIVE, over the REBOOT-class fields only
//     ⛔ A `draft == persisted` test that never looks at EFFECTIVE is the binary-test-over-a-ternary-domain defect
//     this file is pre-registered against (four instances in one session — see tools/probe_ui_model_mutations.py's
//     `arm_backup` docstring for the roll-call). The type system carries the split too: `ICfgLive` can be READ for all
//     covered fields but can only be WRITTEN with `CfgLiveFields`, which structurally cannot carry `ble_mode`.
//
// ⛔ IT IS `config_unsaved`, NEVER `dirty`. `UiState::dirty` (src/firmware_ui_model.h) already means "a repaint is
//    owed", and both are read in the same render pass, so a second meaning on that word would collide there.
//
// ★★ WHY A TYPED SERVICE AND NOT A LOOP OVER `handle_cfg_set` (§3.6.1, and it is the load-bearing constraint):
//    `handle_cfg_set` (src/firmware_config.h) is a per-KEY verb — it parses a string, applies live and writes
//    `/mrcfg` for EACH key. Driving a settings screen through it would mean N validations, N writes, N live applies
//    and therefore PARTIAL SUCCESS (three fields saved, the fourth refused), which makes atomic validation
//    impossible. This service takes the whole candidate, validates ALL of it, writes it ONCE, and only then applies
//    the live-capable half. ⇒ ⛔ never manufacture a command string from the panel.
//
// ⓘ SERIAL/BLE KEEP THEIR IMMEDIATE-WRITE PATH (§3.6.1 says so explicitly, for companion compatibility). This
//   service is transport-agnostic (no `Print`, no strings, no Arduino), and their hook into it is
//   `note_external_write()` — see it for the two INDEPENDENT conflict detectors and why the byte comparison alone
//   still cannot produce last-writer-wins. ⛔ CORRECTED IN PLACE 2026-08-13 ([[B194]]): this sentence said "the
//   UNWIRED one", which was true of §UI-13 and is now false — every USER-INITIATED `/mrcfg` verb calls the hook (see
//   `§notify-every-save` in `src/firmware_config.cpp`); the INTERNAL writers deliberately do not.
//
// ✖ NOT IMPLEMENTED IN THE §UI-13 SLICE THAT ADDED THIS FILE, deliberately and by scope
//   ([[meshroute-mark-done-vs-missing-in-code]]). ⛔⛔ CORRECTED IN PLACE 2026-08-13 (QG round 2): this block was
//   written as a list of things that DO NOT EXIST, and the first two entries have since LANDED — read as-is it told
//   a reader that §UI-14 had not happened, on the same page as a service §UI-14 now drives. The list is kept as the
//   §UI-13 SLICE BOUNDARY it always was, with each entry marked; nothing is deleted, and ⛔ NO obligation it records
//   is dropped — the two that were owed are restated below as what the binding actually DID.
//   · ✅ LANDED (§UI-14) — the SETTINGS renderer / menu / gestures (§3.6.2): `src/firmware_ui.cpp` + the pure model in
//     `src/firmware_ui_model.h`. This file stayed transport- and render-agnostic, which is why it could.
//   · 📝 STILL OWED — team creation, join profiles, the nearby-join flow (§3.6.3/§3.6.4) -> their own slices;
//     provisioning is explicitly NOT a draft field. (The `PROVISION` row §UI-14 renders REFUSES out loud.)
//   · ✅ LANDED (§UI-14, [[B193]]'s first half) — the DEVICE bindings of `ICfgStore` / `ICfgLive`. ⛔ This entry read
//     *"There is no instance of this service on hardware yet, so nothing calls it"*; THAT IS NOW FALSE and is
//     withdrawn here, not deleted. `mrfw::device_cfg_store()` / `device_cfg_live()` are in `src/firmware_config.cpp`
//     and `src/firmware_ui.cpp` constructs the ONE `ConfigService` over them. ★ BOTH OBLIGATIONS THIS ENTRY RECORDED
//     WERE DISCHARGED, and they are restated rather than dropped because a header is where an obligation survives:
//     (1) the store's `load()` IS the §nv-ritual `nv_load_stamped` (load-or-seed/stamp), so an unprovisioned chip
//     opens on its live config instead of being refused — and therefore `CfgOpen::no_record` and the pre-write
//     `nv_failed` are UNREACHABLE ON DEVICE by construction; (2) `apply_live` reproduces `handle_cfg_set`'s OFF->ON
//     `mobile_register_current()` bridge (the `mobile_autoregister` arm, same file so the two cannot drift) — the
//     flag alone does not start a home attachment. ⛔ What is STILL owed is the third bullet below: real flash.
//   · ⛔⛔ ANY QUALIFICATION AGAINST REAL FLASH. Every behaviour below is proved against FAKES in
//     `test/test_firmware_config_service.cpp` — an `ICfgStore` that counts writes and can be told to fail. That is
//     what makes "zero writes", "exactly one write" and "live only after durable success" measurable at all, and it
//     is ALSO the limit of the claim: no NVS/LittleFS write, no wear, and ⛔ no RESET-DURING-WRITE / power-cut
//     behaviour (§3.6.5's "either the complete old record or the complete new record") is exercised here. Those are
//     properties of the DEVICE BINDING and are deferred with it to §UI-14 / bug register B193, where they become a
//     bench check. ⇒ a green suite here says the LOGIC is right, never that the storage is.
//     ⛔⛔ AND THIS ONE IS STILL OWED, WHICH IS WHY IT IS THE BULLET THE OTHERS NOW POINT AT: the binding LANDED with
//     §UI-14, but NOTHING EXERCISES IT — the native suite and both probes drive the service through FAKES (there is
//     no NVS/LittleFS on a host) and the board envs only COMPILE it. ⇒ [[B193]] stays OPEN for exactly this half, and
//     `docs/2026-07-31-bench-test-script.md` Parts 19/20 are its only closure path.
//   · ⓘ `apply_radio_live` (src/firmware_config.cpp) is NOT reached from this slice and that is a MEASURED fact, not
//     an omission: NOT ONE of the four covered fields is a radio parameter (§3.6.2 excludes frequency, transmit
//     power, bandwidth and SF from the one-button editor). It remains the right and only hook for "apply live fields"
//     the moment a radio field becomes covered — at which point `cfg_live_fields` gains a retune class and the
//     `-Wswitch`-guarded name functions below will refuse to build until every arm is written.
#pragma once
#include <cstdint>
#include <cstring>       // memcmp — CfgValues equality (the fingerprint comparison)
#include "device_nv.h"   // mrnv::Blob — THE durable carrier; never rebuilt field-by-field here (U2)

namespace mrfw {

// ---- the COVERED SET -------------------------------------------------------------------------------------------
// ⛔ ONLY FIELDS ALREADY REPRESENTED DURABLY MAY BE COVERED (§3.6.1). Each of the four below is a real `mrnv::Blob`
// member (src/device_nv.h) written by `cfg set` today, so this slice needs no NV-schema change and no kVersion bump.
// Verified against the Blob and against `handle_cfg_set`'s arms, not against a doc:
//   ble_mode             Blob v7  · uint8_t 0=off/1=on/2=periodic · `handle_cfg_set` sets `live = false` -> REBOOT
//   e2e_dm               Blob v10 · uint8_t 0/1 · live via `NodeConfig::e2e_dm`
//   intro_attach         Blob v21 · uint8_t 0/1 · live via `NodeConfig::intro_attach`
//   mobile_autoregister  Blob v18 · uint8_t 0/1 · live via `NodeConfig::mobile_autoregister`
// ✖ DELIBERATELY LEFT OUT, and each for a stated reason rather than by omission:
//   · `team_channel_crypt` — §3.6.2 excludes it (privacy-unsafe as a one-button toggle) AND it is LIVE-ONLY: there is
//     no NV field at all (src/firmware_config.cpp says so at its arm). Covering it would need a device_nv v24 slice.
//   · `ble_period_min` / `ble_pin` — durable, but not finite-choice values; §3.6.2 keeps digit entry off the panel.
//   · everything radio (freq/bw/sf/cr/tx_power/duty) and every identity/provisioning field — §3.6.2/§3.6.3.
enum class CfgField : uint8_t {
    ble_mode            = 0,
    e2e_dm              = 1,
    intro_attach        = 2,
    mobile_autoregister = 3,
};
constexpr uint8_t kCfgFieldCount = 4;

// enum -> string, `default`-LESS so `-Wswitch` (which is `-Werror=switch` here) fails the build when a fifth covered
// field is added. Same discipline and the same reason as `mrnv::peer_put_name`: this project has shipped three
// enum->string defects the byte-identity gate was structurally blind to.
inline const char* cfg_field_name(CfgField f) {
    switch (f) {
        case CfgField::ble_mode:            return "ble_mode";
        case CfgField::e2e_dm:              return "e2e_dm";
        case CfgField::intro_attach:        return "intro_attach";
        case CfgField::mobile_autoregister: return "mobile_autoregister";
    }
    return "?";     // no `default:` arm — unreachable for a valid enumerator, but the function stays total
}

// ★ THE APPLY CLASS IS PER FIELD, AND IT IS WHAT MAKES `reboot_required` DERIVABLE rather than latched.
// `live_now`  = the running node picks it up immediately (a `NodeConfig` member the MAC/app re-reads each use).
// `reboot_at` = persisted only; the running stack cannot adopt it (the BLE stack inits once at boot from
//               `g_ble_mode`), so PERSISTED and EFFECTIVE legitimately disagree until the next boot.
// ⓘ A third class — a RADIO field needing `apply_radio_live` — does not exist yet because no covered field is a
//   radio parameter (see the header note). It is the enumerator to add there, not a `default:` arm here.
enum class CfgApplyClass : uint8_t { live_now, reboot_at };
inline CfgApplyClass cfg_apply_class(CfgField f) {
    switch (f) {
        case CfgField::ble_mode:            return CfgApplyClass::reboot_at;
        case CfgField::e2e_dm:              return CfgApplyClass::live_now;
        case CfgField::intro_attach:        return CfgApplyClass::live_now;
        case CfgField::mobile_autoregister: return CfgApplyClass::live_now;
    }
    return CfgApplyClass::reboot_at;   // total function; a new field defaults to the SAFE class (never applied live
                                       // behind the author's back) — and `-Werror=switch` fires first anyway.
}

// The covered subset as ONE carrier. ⚠ ARRAY-BACKED on purpose: validation, the reload merge and the fingerprint
// comparison all iterate the fields, and `CfgField` is the index — so a fifth field cannot be forgotten by one of
// three hand-written loops. The value-initialised state is NOT a default policy (C2): every live path fills it from
// a `mrnv::Blob`, and `ConfigService` refuses to open at all if it cannot load one.
struct CfgValues {
    uint8_t v[kCfgFieldCount] = {};
    uint8_t&       at(CfgField f)       { return v[static_cast<uint8_t>(f)]; }
    const uint8_t& at(CfgField f) const { return v[static_cast<uint8_t>(f)]; }
};
inline bool operator==(const CfgValues& a, const CfgValues& b) { return memcmp(a.v, b.v, sizeof a.v) == 0; }
inline bool operator!=(const CfgValues& a, const CfgValues& b) { return !(a == b); }

// ★★ THE ONE CONVERSION PATH IN EACH DIRECTION (U2 — never rebuild a carrier field-by-field at a call site).
// `cfg_values_from_blob` is also THE definition of the baseline fingerprint: §3.6.1 asks for a fingerprint, and the
// recorded VALUES are the strongest one available — a 4-byte hash over a 4-byte payload can only lose information,
// and a collision in a conflict detector would silently permit the last-writer-wins the same paragraph forbids.
inline CfgValues cfg_values_from_blob(const mrnv::Blob& b) {
    CfgValues c{};
    c.at(CfgField::ble_mode)            = b.ble_mode;
    c.at(CfgField::e2e_dm)              = b.e2e_dm;
    c.at(CfgField::intro_attach)        = b.intro_attach;
    c.at(CfgField::mobile_autoregister) = b.mobile_autoregister;
    return c;
}
// ⛔ TOUCHES THE COVERED FIELDS AND NOTHING ELSE. That is what lets ONE whole-record write carry a settings change
// without reverting a field this editor does not know about — the leased `channel_ctr`, the team keys, the admin
// replay floor, the radio floor. The blob passed in must be the FRESHLY LOADED record (see `ConfigService::save`).
inline void cfg_values_into_blob(const CfgValues& c, mrnv::Blob& b) {
    b.ble_mode            = c.at(CfgField::ble_mode);
    b.e2e_dm              = c.at(CfgField::e2e_dm);
    b.intro_attach        = c.at(CfgField::intro_attach);
    b.mobile_autoregister = c.at(CfgField::mobile_autoregister);
}

// ---- typed validation ------------------------------------------------------------------------------------------
// ★ TYPED, not textual: the panel and the companion hand over VALUES, so there is no `atoi` here and no
// "any non-zero means on" coercion (`handle_cfg_set` does that at the string boundary, which is where it belongs).
// The domain of each field is the domain its durable byte already has:
//   ble_mode  0..2 — off/on/periodic, exactly as `handle_cfg_set`'s `ble_mode` arm accepts. ⓘ §3.6.2's first menu
//             offers only off/on and notes that `periodic` is to be retired from the firmware; that narrowing is a
//             MENU decision belonging to §UI-14's row, not a narrowing of the durable field's domain — a service
//             shared with serial/BLE must not reject a value those transports still write.
//   the three toggles 0..1 — a 2 is a CALLER BUG, refused loudly rather than coerced (C2).
inline bool cfg_field_valid(CfgField f, uint8_t val) {
    switch (f) {
        case CfgField::ble_mode:            return val <= 2;
        case CfgField::e2e_dm:              return val <= 1;
        case CfgField::intro_attach:        return val <= 1;
        case CfgField::mobile_autoregister: return val <= 1;
    }
    return false;      // total function, and it fails CLOSED: an unknown field is never valid
}
// THE WHOLE CANDIDATE, which is the unit `save` validates BEFORE it writes anything. On false, `first_bad` names the
// field — the renderer needs a row to point at, and a bare bool would make it guess.
inline bool cfg_values_valid(const CfgValues& c, CfgField& first_bad) {
    for (uint8_t i = 0; i < kCfgFieldCount; ++i) {
        const CfgField f = static_cast<CfgField>(i);
        if (!cfg_field_valid(f, c.at(f))) { first_bad = f; return false; }
    }
    return true;
}

// ---- the two seams ---------------------------------------------------------------------------------------------
// The LIVE-CLASS fields, and this type is a PROOF rather than a convenience: `ble_mode` is structurally absent, so
// "the panel applied a reboot-only field live" is not expressible through `ICfgLive::apply_live` at all.
struct CfgLiveFields {
    bool e2e_dm              = false;
    bool intro_attach        = false;
    bool mobile_autoregister = false;
};
// The ONE conversion path onto it (U2). ⓘ It is derived from `CfgValues` rather than assembled at the call site so a
// new live-class field cannot be applied by one caller and forgotten by another.
inline CfgLiveFields cfg_live_fields(const CfgValues& c) {
    CfgLiveFields f{};
    f.e2e_dm              = c.at(CfgField::e2e_dm) != 0;
    f.intro_attach        = c.at(CfgField::intro_attach) != 0;
    f.mobile_autoregister = c.at(CfgField::mobile_autoregister) != 0;
    return f;
}

// THE DURABLE SEAM. One record, whole-blob R/W, exactly as `/mrcfg` already is — so "one durable configuration
// write" is one `save()` call here and cannot become a per-field loop further down.
// ⚠ `load` must be the §nv-ritual (load-or-seed + version stamp) on hardware, NOT a bare `mrnv::load`: this service
// REFUSES to open when the load fails, because without the current record it cannot preserve the non-covered fields.
struct ICfgStore {
    virtual ~ICfgStore() = default;
    virtual bool load(mrnv::Blob& out) = 0;        // false = no usable record (absent / version-rejected / read error)
    virtual bool save(const mrnv::Blob& b) = 0;    // false = THE WRITE FAILED (nothing may be applied live)
};
// THE EFFECTIVE SEAM, and it is DELIBERATELY ASYMMETRIC (see the header's three-state note): every covered field can
// be READ back (`reboot_required` needs the effective `ble_mode`), while only the live-class half can be WRITTEN.
struct ICfgLive {
    virtual ~ICfgLive() = default;
    virtual CfgValues effective() const = 0;              // the RUNNING values (NodeConfig + g_ble_mode on device)
    virtual void apply_live(const CfgLiveFields& f) = 0;   // ★ called ONLY after the durable write returned success
};

// ---- explicit outcomes (requirement 9) -------------------------------------------------------------------------
enum class CfgOpen : uint8_t {
    ok,            // snapshotted the persisted covered fields; draft = baseline
    already_open,  // ⛔ A NO-OP, AND THAT IS THE POINT: re-entering SETTINGS after `BACK` or a blank MUST NOT reset
                   // the draft (§3.6.1 — "silently discarding because attention timed out is forbidden"). Losing a
                   // draft on re-open would be exactly that discard, arriving through the door instead of the timer.
    no_record,     // the store could not produce a record -> nothing opened, no draft, no baseline (C2 fail loud)
};
enum class CfgSet : uint8_t {
    ok,            // the RAM draft moved. ⛔ no live mutation, no radio retune, no flash write
    not_open,
    bad_value,     // outside the field's typed domain -> the draft is UNTOUCHED (fail closed)
};
enum class CfgSave : uint8_t {
    saved,         // exactly ONE durable write, then the live-class fields applied
    saved_reboot,  // ★ ditto AND a reboot-class field now differs from EFFECTIVE. It IS durably saved and is NO
                   // LONGER unsaved — `reboot_required()` and `config_unsaved()` are independent facts.
    no_change,     // draft == persisted -> ZERO NV writes (§3.6.1's no-op save)
    invalid,       // the candidate failed validation -> ZERO NV writes, draft + marker retained
    conflict,      // `/mrcfg` moved under the draft -> ZERO NV writes, REFUSED; needs RELOAD or DISCARD
    nv_failed,     // the single write (or the pre-write reload) failed -> old effective/persisted state kept, draft
                   // AND marker retained
    not_open,
};
enum class CfgRefresh : uint8_t { ok, not_open, nv_failed };   // the DISCARD / RELOAD outcome

inline const char* cfg_open_name(CfgOpen r) {
    switch (r) {
        case CfgOpen::ok:           return "ok";
        case CfgOpen::already_open: return "already_open";
        case CfgOpen::no_record:    return "no_record";
    }
    return "?";
}
inline const char* cfg_set_name(CfgSet r) {
    switch (r) {
        case CfgSet::ok:        return "ok";
        case CfgSet::not_open:  return "not_open";
        case CfgSet::bad_value: return "bad_value";
    }
    return "?";
}
inline const char* cfg_save_name(CfgSave r) {
    switch (r) {
        case CfgSave::saved:        return "saved";
        case CfgSave::saved_reboot: return "saved_reboot";
        case CfgSave::no_change:    return "no_change";
        case CfgSave::invalid:      return "invalid";
        case CfgSave::conflict:     return "conflict";
        case CfgSave::nv_failed:    return "nv_failed";
        case CfgSave::not_open:     return "not_open";
    }
    return "?";
}
inline const char* cfg_refresh_name(CfgRefresh r) {
    switch (r) {
        case CfgRefresh::ok:        return "ok";
        case CfgRefresh::not_open:  return "not_open";
        case CfgRefresh::nv_failed: return "nv_failed";
    }
    return "?";
}
// ★ THE TWO PANEL HEADLINES §3.6.1 NAMES, AND ONLY THOSE TWO. Formatted in this pure unit for the same reason
// `emg_attempt_line` is (src/firmware_ui_model.h): a native test can then assert the VISIBLE BYTES. ⛔ The other
// outcomes get an EMPTY string deliberately — the spec names no headline for them, and inventing panel text here
// would be exactly the substitution the provenance rules forbid. §UI-14 owns those strings, and must call THIS for
// the two that are ruled rather than re-spelling them.
inline const char* cfg_save_panel(CfgSave r) {
    switch (r) {
        case CfgSave::conflict:  return "CFG! RELOAD";
        case CfgSave::nv_failed: return "SAVE FAILED";
        case CfgSave::saved:
        case CfgSave::saved_reboot:
        case CfgSave::no_change:
        case CfgSave::invalid:
        case CfgSave::not_open:  return "";
    }
    return "";
}

// ---- the service -----------------------------------------------------------------------------------------------
// RAM: ⛔ **TWO** `CfgValues` (4 B each — `_baseline` and `_draft`), two references and two flags. ⚠ CORRECTED
// 2026-08-13: this line said "three `CfgValues`", which is wrong and would have been read as a budget — the THIRD
// state (EFFECTIVE) is deliberately NOT stored here, it is read on demand through `ICfgLive::effective()` so there is
// no copy of it to go stale. The draft is a second copy of the covered fields BY DESIGN — that is what makes
// "changing a row changes the RAM draft only" true.
class ConfigService {
  public:
    ConfigService(ICfgStore& store, ICfgLive& live) : _store(store), _live(live) {}

    // Snapshot the persisted covered fields and RECORD THE BASELINE. See `CfgOpen::already_open`.
    CfgOpen open() {
        if (_open) return CfgOpen::already_open;
        mrnv::Blob b{};                                   // value-initialised: a failed load may still have written
        if (!_store.load(b)) return CfgOpen::no_record;    // into it (device_nv.h's §nv-ritual warning)
        _baseline = cfg_values_from_blob(b);
        _draft    = _baseline;
        _open     = true;
        _conflict = false;
        return CfgOpen::ok;
    }

    bool is_open() const { return _open; }
    const CfgValues& draft()    const { return _draft; }
    const CfgValues& baseline() const { return _baseline; }   // the recorded fingerprint (§3.6.1)

    // DRAFT vs BASELINE. ⛔ Not `dirty` — see the header.
    bool config_unsaved() const { return _open && _draft != _baseline; }
    // PERSISTED vs BASELINE, latched by `note_external_write` or by a refused save. Cleared only by RELOAD/DISCARD —
    // ★ and `save()` gate 2a HONOURS THE LATCH, so "clears only by RELOAD/DISCARD" is a property of the object and not
    // just of this accessor. ⛔ It used to be only of the accessor: `save()` consulted the byte comparison alone, so a
    // revert-after-notify saved with `conflict()` still true. See gate 2a.
    bool conflict() const { return _conflict; }
    // ★ BASELINE vs EFFECTIVE, over the reboot-class fields ONLY, and DERIVED rather than latched: a latch would
    // still claim "reboot required" after the operator set the value back and saved again, and it would have to be
    // cleared by somebody. Derived, it is automatically true from the save until the reboot (nothing writes
    // `g_ble_mode` at runtime — verified in src/fw_main.cpp: boot restore reads it, `cfg set ble_mode` does not
    // touch it) and automatically false once the two agree again.
    // ⚠ Reads `_baseline`, not a fresh NV load: a renderer may call this every frame and a flash read per frame is
    // not acceptable. While `conflict()` is true the baseline is knowingly stale — the UI must resolve that first.
    bool reboot_required() const {
        if (!_open) return false;
        const CfgValues eff = _live.effective();
        for (uint8_t i = 0; i < kCfgFieldCount; ++i) {
            const CfgField f = static_cast<CfgField>(i);
            if (cfg_apply_class(f) != CfgApplyClass::reboot_at) continue;
            if (_baseline.at(f) != eff.at(f)) return true;
        }
        return false;
    }

    // Change ONE row: RAM only. ⛔ no radio retune, no live mutation, no flash write (§3.6.1).
    CfgSet set(CfgField f, uint8_t val) {
        if (!_open) return CfgSet::not_open;
        if (!cfg_field_valid(f, val)) return CfgSet::bad_value;   // fail closed: the draft is not half-written
        _draft.at(f) = val;
        return CfgSet::ok;
    }
    // Stage a WHOLE candidate (a companion bulk write, a future preset) WITHOUT per-field validation — deliberately:
    // the atomic validation that matters happens in `save()`, which is the only place a write can follow it. `set()`
    // refusing early is a convenience for the one-row editor, never the guarantee.
    void stage_all(const CfgValues& c) { if (_open) _draft = c; }

    // ★★ THE COMMIT. The ORDER of the five gates is the contract:
    //   1.  validate the WHOLE candidate           -> `invalid`,   ZERO writes;
    //   2a. THE LATCH — `_conflict` is already raised -> `conflict`, ZERO writes, WITHOUT reading the store;
    //   2b. reload `/mrcfg` and compare the covered fields with the baseline -> `conflict`, ZERO writes. ⛔ This is
    //       the detector that makes last-writer-wins IMPOSSIBLE even when nobody wired `note_external_write`;
    //   3.  no-op                                  -> `no_change`, ZERO writes. Checked AFTER both conflict gates, so
    //       a moved persisted record can never be reported as "nothing to do";
    //   4.  ONE whole-record write of the RELOADED blob with only the covered fields overwritten -> then, and only
    //       then, the live apply. ⚠ The freshly loaded blob is used precisely so a `channel_ctr` lease (or a team key,
    //       or an admin counter) written since `open()` is CARRIED FORWARD instead of being reverted by a stale copy.
    // ★★★ WHY 2a EXISTS AND WHY 2b IS NOT ENOUGH — A THIRD STATE THAT NEITHER BRANCH OF 2b NAMES. The byte comparison
    //   and the latch are TWO DIFFERENT NOTIONS OF "conflict", and 2b alone consults only the first: an external
    //   writer can change a covered field (`note_external_write` raises the latch) and then RESTORE the baseline
    //   bytes, at which point 2b's comparison PASSES while `conflict()` still reports true — so the object would
    //   report a conflict and SAVE ANYWAY, which is the last-writer-wins outcome §3.6.1 forbids, reached by a
    //   different route (the operator was told to RELOAD or DISCARD and neither happened). ⇒ the states are
    //   {bytes differ} · {bytes match, latch clear} · {BYTES MATCH, LATCH SET}, and only the third needs 2a.
    //   ⛔ Do not "simplify" 2a away on the strength of a green run: 2b keeps the suite green without it except for
    //   the change→notify→revert→SAVE case, which is exactly why that case exists (mutation C32).
    // ★ A fact is established by the ACT: `_baseline` moves and the live apply happens only after `save` returned
    //   true — never before, and never on any of the four refusal paths.
    CfgSave save() {
        if (!_open) return CfgSave::not_open;
        CfgField bad = CfgField::ble_mode;
        if (!cfg_values_valid(_draft, bad)) return CfgSave::invalid;      // (1) ZERO writes
        if (_conflict) return CfgSave::conflict;                          // (2a) THE LATCH, ZERO writes
        mrnv::Blob now{};
        if (!_store.load(now)) return CfgSave::nv_failed;                 // cannot preserve the rest -> refuse (0 writes)
        const CfgValues persisted = cfg_values_from_blob(now);
        if (persisted != _baseline) { _conflict = true; return CfgSave::conflict; }   // (2b) ZERO writes
        if (_draft == persisted) return CfgSave::no_change;               // (3) ZERO writes
        cfg_values_into_blob(_draft, now);                               // (4) covered fields only
        if (!_store.save(now)) return CfgSave::nv_failed;                //     EXACTLY ONE write attempt
        _baseline = _draft;                                              //     no longer unsaved
        _live.apply_live(cfg_live_fields(_draft));                       //     live ONLY after durable success
        return reboot_required() ? CfgSave::saved_reboot : CfgSave::saved;
    }

    // DISCARD — reload the persisted values into the draft and clear the marker (§3.6.1, its exact wording).
    CfgRefresh discard() {
        if (!_open) return CfgRefresh::not_open;
        mrnv::Blob b{};
        if (!_store.load(b)) return CfgRefresh::nv_failed;   // the draft SURVIVES an unreadable store (C2)
        _baseline = cfg_values_from_blob(b);
        _draft    = _baseline;
        _conflict = false;
        return CfgRefresh::ok;
    }

    // RELOAD — the conflict's OTHER resolution, and it is a THREE-WAY MERGE of exactly the three states: a field the
    // operator did NOT edit adopts the newer persisted value; a field they DID edit keeps their value; the baseline
    // becomes the new persisted record.
    // ★★ OWNER-RULED 2026-08-13 ([[B192]], ledger §1.22) — THIS SHAPE IS THE RULED ONE, in reported form: RELOAD
    // performs the three-way merge; fields UNCHANGED in the draft adopt the current persisted values, fields EDITED in
    // the draft remain unsaved in the draft, and DISCARD remains the explicit full reset. ⛔ Do not re-open or
    // "improve" the merge. ⛔ CORRECTED IN PLACE: this block previously read *"DESIGN CHOICE, REPORTED AS ONE … this
    // shape is not ruled and is registered as an owner decision"*, which was accurate when written and is now FALSE.
    // The REASON it is not "keep the whole draft and re-baseline" is unchanged and is why it was ruled this way: that
    // would re-save the operator's stale value for a field the companion had just changed and they never touched —
    // i.e. it would resurrect the last-writer-wins §3.6.1 explicitly forbids. Making it identical to DISCARD was the
    // other candidate and costs the operator's typing for no safety gain.
    // ⚠ THE RULING SETTLES BEHAVIOUR ONLY. RELOAD's NV / power-cut qualification is still DEFERRED to the UI-14 device
    // binding and [[B193]] — everything here is proved against a FAKE store (see the header's not-implemented block).
    CfgRefresh reload() {
        if (!_open) return CfgRefresh::not_open;
        mrnv::Blob b{};
        if (!_store.load(b)) return CfgRefresh::nv_failed;
        const CfgValues now = cfg_values_from_blob(b);
        for (uint8_t i = 0; i < kCfgFieldCount; ++i) {
            const CfgField f = static_cast<CfgField>(i);
            if (_draft.at(f) == _baseline.at(f)) _draft.at(f) = now.at(f);   // untouched -> adopt theirs
        }
        _baseline = now;
        _conflict = false;
        return CfgRefresh::ok;
    }

    // ★★ THE OTHER CONFLICT DETECTOR — the IMMEDIATE one, and the reason there are two.
    // Serial and BLE keep writing `/mrcfg` directly (§3.6.1), so the marker must be raisable the moment they do,
    // without this service polling flash. They call this with the record they just wrote; the covered fields are
    // compared with the BASELINE.
    // ⛔ THE NEGATIVE HALF IS THE POINT AND IS STRUCTURAL, not a filter list: only the four covered fields are ever
    // extracted, so an external write that moved `channel_ctr`, `team_local_id`, the admin replay floor, the radio
    // floor or anything else raises NOTHING. And a RUNTIME change — a route, a registration, the battery, an unread
    // count — has no `/mrcfg` representation at all, so it cannot even reach this function.
    void note_external_write(const mrnv::Blob& persisted_now) {
        if (!_open) return;
        if (cfg_values_from_blob(persisted_now) != _baseline) _conflict = true;
    }

    // ★ `BACK` and blanking PRESERVE THE DRAFT (§3.6.1: silently discarding because attention timed out is
    // FORBIDDEN). These two are the named seams §UI-14 must call, and they are draft-preserving no-ops BY
    // CONSTRUCTION — they exist so the renderer has an honest place to route those events instead of inventing a
    // reset. ⛔ Neither may ever grow into a `discard()`; the re-entry case is covered by `CfgOpen::already_open`.
    void on_back()  {}
    void on_blank() {}

  private:
    ICfgStore& _store;
    ICfgLive&  _live;
    CfgValues  _baseline{};     // PERSISTED as of the last open / save / reload / discard = the fingerprint
    CfgValues  _draft{};        // §3.6.1's ConfigDraft
    bool       _open     = false;
    bool       _conflict = false;
};

}  // namespace mrfw
