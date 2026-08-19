// MeshRoute — src/firmware_join_profiles.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-15 slice 2 — THE `/mrjoin` PROFILE STORE. Spec:
// docs/superpowers/specs/2026-08-18-ui15-provisioning-implementation-plan.md (v6 — the authoritative version) §3,
// dispatch brief docs/superpowers/plans/2026-08-19-ui15-slice2-mrjoin-store.md.
//
// ★★★ WHAT THIS SLICE IS: FOUR operator-authored presets and the four verbs that edit them. ⛔ IT IS NOT UI. No
//     screen, no gesture, no `Screen`/`Settings` arm, and nothing here reads or writes `/mrcfg`. §UI-15 slice 6's
//     OLED will only ever SELECT a profile and hand it to `mrfw::JoinService` (slice 1) — which is why the one thing
//     this file exports besides the store is `join_request_from_profile`.
//
// ★★ WHY THE LOGIC IS HERE AND NOT IN THE VERB, and it is the same argument slice 1 and §PROV-TX both made:
//    `src/firmware_config.cpp` is compiled by NEITHER the native suite (`test_build_src = no`) NOR the simulator, and
//    no corpus scenario runs a console verb. ⇒ a write-count property decided inside `handle_joinprofile` would be
//    unreachable by EVERY automated gate. This header is pure — no `Print`, no Arduino, no globals — so
//    `test/test_firmware_join_profiles.cpp` can COUNT the writes, which is the only way the absent/corrupt matrix
//    below is a measurement rather than a claim.
//
// ★★★ THE ABSENT / CORRUPT MATRIX IS THE CONTRACT (spec §3, normative). ⛔ A silent fallback would make a corrupted
//     store indistinguishable from a fresh device — the operator would see `NO PROFILES`, retype four presets, and
//     never learn the flash ate them.
//        verb            ABSENT                                          CORRUPT
//        set             seeds a valid empty record, applies, ONE write  refused (store_invalid), ZERO writes
//        list            `empty`  -> NO PROFILES (ordinary)              refused (store_invalid) -> PROFILE STORE INVALID
//        clear           `empty`  -> NO CHANGE, ★ ZERO WRITES            ⛔⛔ refused — MUST NOT recover
//        reset confirm   `empty`  -> ★ ZERO WRITES                       ★ the ONLY recovery path — ONE write
//     ★★ AND A **THIRD** STORE STATE, ADDED 2026-08-19 BY THE QG HOLD: `io_failed` — the backend would not open at
//        all (LittleFS would not mount / NVS would not open). ALL FOUR VERBS REFUSE IT WITH ZERO WRITES, including
//        `reset confirm`: a store that cannot be READ must not be blind-rewritten, because the four profiles may be
//        perfectly intact behind a transient mount failure. ⛔ It is NOT folded into CORRUPT — that would print a
//        remedy (`reset confirm`, which discards all four slots) for a fault it cannot repair.
//     ⇒ ⛔ `clear` MUST NEVER BE A BACKDOOR REPAIR: it would have to rewrite three slots it could not read.
//     ⇒ ⛔ neither `clear` nor `reset` reports an ERROR for an absent store — a fresh device is not a fault, and a
//       "helpful" write there would also defeat the coalescing rule below.
//
// ★★ THE COALESCING RULE, AND WHY IT LIVES HERE RATHER THAN IN `mrnv::save_join`: every verb has ALREADY loaded the
//    record (it must, to edit one slot of four), so the whole-record byte compare is FREE at this level and costs a
//    second flash READ at the other. `/mrcfg`'s `save()` pays that read because its callers do not load first; this
//    one must not. ⇒ a re-`set` of identical values performs ZERO writes, and that is COUNTED in the suite.
//
// ⛔⛔ THE LIMIT OF EVERY CLAIM BELOW, in the words §PROV-TX and slice 1 both had to use: these properties are proved
//    against a FAKE store that counts calls. ⛔ No NVS/LittleFS write, no flash wear, and NO reset-during-write
//    behaviour is exercised ([[B193]]) — a `save` that reports failure may have written PARTIALLY, so ⛔ nothing here
//    or in its console voice may say "no flash was changed". §UI-15 slice 7 owns the power-cut question.
#pragma once
#include <cstdint>
#include <cmath>                    // std::isfinite — THE non-finite boundary refusal (see validate_profile)
#include <cstring>                  // memcmp/memcpy — the byte-identical write guard + the name copy
#include "device_nv.h"              // mrnv::JoinBlob / JoinProfile / JoinRead — THE durable carrier (U2)
#include "firmware_join_service.h"  // mrfw::JoinRequest + validate_join — ⛔ THE ONE validation authority (U1)
#include "protocol_constants.h"     // protocol::khz_to_hz — the ONE kHz -> Hz conversion (U1/U2)

namespace mrfw {

// ---- units ------------------------------------------------------------------------------------------------------
// ★★ MHz -> Hz IS ITS OWN CONVERSION AND ⛔ MUST NOT BE COMPOSED FROM THE TWO EXISTING HELPERS. `khz_to_hz(
//    mhz_to_khz(869.4625))` ROUNDS TWICE: 869.4625 MHz -> 869463 kHz -> 869463000 Hz, which is 500 Hz off the
//    carrier this build actually runs on. That double-round is precisely the defect the Hz-not-kHz ruling exists to
//    prevent, so the composition is named here as forbidden rather than left to be rediscovered.
// ⓘ WHY IT IS NOT IN `lib/core/protocol_constants.h` BESIDE ITS TWO SIBLINGS, stated plainly: this slice's gate
//   requires `git diff -- lib/` to be EMPTY (D2), and a lib/core edit would re-run the whole 36-row corpus for a
//   helper only `src/` uses. ⇒ it lives here, spelled in the SAME idiom (`double` in, `+ 0.5` round, `uint32_t` out)
//   so that moving it later is a copy, not a re-derivation. ⛔ Do not "improve" it to `llround`.
// ⛔ NON-FINITE INPUT IS UNDEFINED HERE — `static_cast<uint32_t>(NaN)` is UB. `validate_profile` REFUSES a
//    non-finite before any caller can reach this. That ordering is the trap this slice exists around; see below.
inline constexpr uint32_t mhz_to_hz(double mhz) { return static_cast<uint32_t>(mhz * 1000000.0 + 0.5); }

// ---- the durable seam -------------------------------------------------------------------------------------------
// ★ A SECOND STORE INTERFACE IS NOT A FORK OF `ICfgStore` (U1 was checked first): `ICfgStore` carries an `mrnv::Blob`
//   and answers `bool`, and BOTH differ here — a different record, and a THREE-valued read whose whole purpose is to
//   distinguish absent from corrupt. Widening `ICfgStore` to a tri-state would have changed `/mrcfg`'s and `join`'s
//   behaviour inside a storage slice (C1).
struct IJoinStore {
    virtual ~IJoinStore() = default;
    virtual mrnv::JoinRead load(mrnv::JoinBlob& out) = 0;
    virtual bool save(const mrnv::JoinBlob& b) = 0;   // false = THE WRITE FAILED. ⛔ Never "nothing was written".
};

// ---- explicit outcomes ------------------------------------------------------------------------------------------
// ⛔ `unchanged` AND `empty` ARE BOTH SUCCESSES AND ARE NOT THE SAME SUCCESS: `unchanged` means the record already
//    said exactly this (the coalescing arm); `empty` means there is no record at all (the fresh-device arm). Both
//    perform ZERO writes, and collapsing them would make the console unable to tell an operator which is true.
enum class ProfileVerdict : uint8_t {
    ok,          // the verb applied and performed EXACTLY ONE write
    unchanged,   // the stored record already matched, byte for byte -> ★ ZERO writes (the flash-wear guard)
    empty,       // the store is ABSENT — an ordinary fresh-device state, ⛔ never an error, ★ ZERO writes
    refused,     // a validation / index / confirm / corrupt-store refusal: ⛔ ZERO writes
    nv_failed,   // the ONE save attempt failed
};
enum class ProfileErr : uint8_t {
    none,
    bad_index,       // slot outside 1..kJoinProfiles (0 and 5 are the off-by-one ends)
    invalid_layer,   // \  the four DOMAIN arms, mapped 1:1 from slice 1's JoinErr — ⛔ never re-derived here
    invalid_freq,    //  |
    invalid_bw,      //  |
    invalid_sf,      // /
    not_finite,      // ★★ THIS SLICE'S OWN refusal — see validate_profile. ⛔ NOT a JoinErr arm.
    name_too_long,   // more label bytes than the slot holds — refused, ⛔ never truncated (C2)
    store_invalid,   // the record is present but unreadable -> PROFILE STORE INVALID
    // ★★★ ⛔ NOT A SYNONYM FOR `store_invalid`, and the difference is the operator's next action. `store_invalid`
    //     means the RECORD is wrong and `joinprofile reset confirm` rewrites it. `store_io_failed` means the STORE
    //     COULD NOT BE OPENED — nothing is known about the record, so the remedy is a DEVICE one (`faults`,
    //     `factory_reset confirm`, a reflash), and telling him to type `reset confirm` would invite him to discard
    //     four possibly-intact profiles because a mount failed. See mrnv::JoinRead::io_failed.
    store_io_failed,
    needs_confirm,   // `reset` without `confirm`
    nv_save_failed,  // carried by the nv_failed verdict
};
// enum -> string, `default`-LESS so `-Werror=switch` fails the build when an arm is added. Same discipline and the
// same reason as `mrnv::peer_put_name`: this project has shipped THREE enum->string defects the byte-identity gate
// was structurally blind to.
inline const char* profile_verdict_name(ProfileVerdict v) {
    switch (v) {
        case ProfileVerdict::ok:        return "ok";
        case ProfileVerdict::unchanged: return "unchanged";
        case ProfileVerdict::empty:     return "empty";
        case ProfileVerdict::refused:   return "refused";
        case ProfileVerdict::nv_failed: return "nv_failed";
    }
    return "?";     // total function; -Werror=switch fires before this is reachable for a valid enumerator
}
inline const char* profile_err_name(ProfileErr e) {
    switch (e) {
        case ProfileErr::none:           return "none";
        case ProfileErr::bad_index:      return "bad_index";
        case ProfileErr::invalid_layer:  return "invalid_layer";
        case ProfileErr::invalid_freq:   return "invalid_freq";
        case ProfileErr::invalid_bw:     return "invalid_bw";
        case ProfileErr::invalid_sf:     return "invalid_sf";
        case ProfileErr::not_finite:     return "not_finite";
        case ProfileErr::name_too_long:  return "name_too_long";
        case ProfileErr::store_invalid:  return "store_invalid";
        case ProfileErr::store_io_failed: return "store_io_failed";
        case ProfileErr::needs_confirm:  return "needs_confirm";
        case ProfileErr::nv_save_failed: return "nv_save_failed";
    }
    return "?";
}
struct ProfileResult {
    ProfileVerdict verdict = ProfileVerdict::refused;
    ProfileErr     err     = ProfileErr::none;
};

// ---- validation -------------------------------------------------------------------------------------------------
// ★★★ THE NON-FINITE GUARD IS **NEW SURFACE**, AND THAT IS WHY IT IS LEGITIMATE. `validate_join` ACCEPTS a `nan`
//     freq/bw — deliberately, documented at `firmware_config_parse.h:95-99`, registered as [[B216]]: the predicates
//     are the NEGATION of the reject-conditions, so `!(nan < 100.0 || nan > 1000.0)` is TRUE. Slice 1 could carry
//     that unchanged because its request holds a `double` all the way into `Blob::freq_mhz`.
//     ⛔⛔ THIS RECORD IS INTEGRAL, AND `static_cast<uint32_t>(NaN)` IS UNDEFINED BEHAVIOUR. ⇒ `set` MUST refuse a
//     non-finite value, and it may: `joinprofile` is a verb that did not exist yesterday, so it carries NO
//     byte-identity obligation. ⛔ WHAT IS FORBIDDEN is "fixing" B216 in the SHARED validator — that would change
//     what the EXISTING `join` verb does, inside a storage slice (C1). ⇒ the refusal is HERE, at the profile
//     boundary, and `validate_join` is called unchanged one line below.
// ★ IT RUNS **FIRST**, before `validate_join`, so the operator is told the truth: a NaN is not an out-of-range
//   frequency, it is not a frequency at all. (Either order refuses; only this order names the right fault.)
inline ProfileErr validate_profile(const JoinRequest& req, uint8_t name_len) {
    if (!std::isfinite(req.freq_mhz) || !std::isfinite(req.bw_khz)) return ProfileErr::not_finite;
    // ⛔ ONE AUTHORITY, NEVER A SECOND RANGE TABLE (U1): the layer/freq/bw/SF domains are slice 1's, which are in
    //    turn `firmware_config_parse.h`'s — the same four `phy_args_in_range` composes for the console. The switch
    //    below is a 1:1 RELABEL, not a re-check, and it is `default`-less so a new JoinErr arm fails the build.
    switch (validate_join(req)) {
        case JoinErr::none:           break;
        case JoinErr::invalid_layer:  return ProfileErr::invalid_layer;
        case JoinErr::invalid_freq:   return ProfileErr::invalid_freq;
        case JoinErr::invalid_bw:     return ProfileErr::invalid_bw;
        case JoinErr::invalid_sf:     return ProfileErr::invalid_sf;
        // The two STORE arms of slice 1's enum cannot be produced by `validate_join` (it touches no store). Listed
        // for exhaustiveness, and mapped to the nearest honest answer rather than to `none`.
        case JoinErr::nv_load_failed:
        case JoinErr::nv_save_failed: return ProfileErr::store_invalid;
    }
    if (name_len > sizeof(mrnv::JoinProfile::name)) return ProfileErr::name_too_long;   // C2: refuse, ⛔ never truncate
    return ProfileErr::none;
}

// ---- the ONE composition path (U2 — ⛔ never rebuild the carrier field-by-field at a second site) ----------------
// ★ THE SLOT IS ZEROED FIRST, WHOLE. That is not tidiness: the name TAIL and every unused byte must be
//   DETERMINISTIC, or the byte-identical write guard would fire on garbage and rewrite flash for nothing. (The same
//   reasoning `peer_rec_merge` states for `PeerRec`.) ⛔ Note this differs from `blob_put_static_join`, which
//   deliberately does NOT zero `/mrcfg`'s name bytes — there, preserving the pre-slice bytes was the contract; here,
//   the record is new and determinism is.
// ⛔ CALL ONLY AFTER `validate_profile` RETURNED `none` — `mhz_to_hz` is UB on a non-finite input.
inline void join_profile_put(mrnv::JoinProfile& p, const JoinRequest& req, const char* name, uint8_t name_len) {
    p = mrnv::JoinProfile{};
    p.present    = 1;
    p.layer      = req.layer;              // the FULL byte; the wire leaf nibble is DERIVED (join_leaf_of_layer)
    p.routing_sf = req.routing_sf;
    p.freq_hz    = mhz_to_hz(req.freq_mhz);
    p.bw_hz      = meshroute::protocol::khz_to_hz(req.bw_khz);
    if (name && name_len) {
        if (name_len > sizeof p.name) name_len = static_cast<uint8_t>(sizeof p.name);   // clamp: validate_profile
        memcpy(p.name, name, name_len);                                                 // already refused this case
        p.name_len = name_len;
    }
}

// ★★ THE REVERSE CONVERSION, AND IT IS THE **ONLY** ONE (U2). `list` renders MHz/kHz and §UI-15 slice 6's screen
//    hands the result straight to `JoinService::apply_join` — if either re-derived the division, the two could
//    disagree about the carrier a profile means. ⓘ The round trip is EXACT for every value this store can hold:
//    `mhz_to_hz` rounds to a whole Hz and `freq_hz / 1e6` is exact in `double` well past 1 GHz.
inline JoinRequest join_request_from_profile(const mrnv::JoinProfile& p) {
    JoinRequest r{};
    r.layer      = p.layer;
    r.routing_sf = p.routing_sf;
    r.freq_mhz   = static_cast<double>(p.freq_hz) / 1000000.0;
    r.bw_khz     = static_cast<double>(p.bw_hz)   / 1000.0;
    return r;
}

// How many of the four slots hold a profile. `0` on a VALID record is `NO PROFILES` just as an ABSENT store is —
// the two are different facts with the same operator-facing answer, and the console renders them with one line.
inline uint8_t join_profile_count(const mrnv::JoinBlob& b) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < mrnv::kJoinProfiles; ++i) if (b.prof[i].present) ++n;
    return n;
}
// 1-based, because the OPERATOR's slot numbers are 1..4 and the array's are 0..3. ★ Named so the off-by-one exists
// in ONE place; both ends (0 and 5) are pinned in the suite.
inline bool valid_profile_slot(long slot1) { return slot1 >= 1 && slot1 <= static_cast<long>(mrnv::kJoinProfiles); }

// ---- the two UNREADABLE answers -------------------------------------------------------------------------------
// ★★ NAMED ONCE (U1) because FOUR verbs ask the same question and one of them — `reset` — then answers it
//    DIFFERENTLY. Spelling `st == invalid || st == io_failed` at four sites is how the two would drift apart.
// ⛔ `absent` IS NOT UNREADABLE. A fresh device read its store perfectly; there was simply nothing in it.
inline bool join_read_unreadable(mrnv::JoinRead st) {
    return st == mrnv::JoinRead::invalid || st == mrnv::JoinRead::io_failed;
}
// The 1:1 relabel. ⛔ Never collapse the two into `store_invalid`: the console prints a DIFFERENT remedy for each,
// and a wrong remedy here costs the operator all four presets.
inline ProfileErr profile_err_of_unreadable(mrnv::JoinRead st) {
    return st == mrnv::JoinRead::io_failed ? ProfileErr::store_io_failed : ProfileErr::store_invalid;
}

// ---- the service --------------------------------------------------------------------------------------------
// RAM: one reference. ⛔ No cached record and no draft — each verb is a single load-edit-store transaction, so there
// is no state between calls to go stale (and no second copy of the record to disagree with flash).
class JoinProfileService {
  public:
    explicit JoinProfileService(IJoinStore& store) : _store(store) {}

    // `joinprofile list` — READ ONLY. ⛔ Performs ZERO writes on every path, including a corrupt store: repairing on
    // a read is exactly the silent fallback this record exists to avoid.
    // On `empty` and on `refused`, `out` is left as a VALID EMPTY record rather than as whatever the failed read
    // deposited — a caller that renders anyway shows nothing, never garbage.
    ProfileResult list(mrnv::JoinBlob& out) {
        ProfileResult r{};
        const mrnv::JoinRead st = _store.load(out);
        if (st == mrnv::JoinRead::ok) { r.verdict = ProfileVerdict::ok; return r; }
        mrnv::join_blob_init(out);
        if (st == mrnv::JoinRead::absent) { r.verdict = ProfileVerdict::empty; return r; }
        r.err = profile_err_of_unreadable(st);  // verdict stays `refused` (the default)
        return r;
    }

    // `joinprofile set <1..4> layer= freq=<MHz> bw=<kHz> sf= [name="…"]`
    // ★★★ THE ORDER IS THE CONTRACT: index -> validate -> load -> compose ONE candidate -> byte compare -> AT MOST
    //     ONE save. A bad index or a domain refusal costs ⛔ ZERO loads and ZERO writes; a corrupt store costs ZERO
    //     writes; an identical re-set costs ZERO writes; ★ an ABSENT store costs EXACTLY ONE (the seed and the slot
    //     land in the same write, ⛔ never two).
    ProfileResult set(long slot1, const JoinRequest& req, const char* name, uint8_t name_len) {
        ProfileResult r{};
        if (!valid_profile_slot(slot1)) { r.err = ProfileErr::bad_index; return r; }   // ⛔ 0 loads, 0 writes
        const ProfileErr ve = validate_profile(req, name_len);
        if (ve != ProfileErr::none) { r.err = ve; return r; }                          // ⛔ 0 loads, 0 writes

        mrnv::JoinBlob cur{};
        const mrnv::JoinRead st = _store.load(cur);
        if (join_read_unreadable(st)) { r.err = profile_err_of_unreadable(st); return r; }   // ⛔ 0 writes
        // ★ THE SEED. An absent store becomes a valid EMPTY record IN RAM, and the edited record is what gets
        //   written — one write, not a seed-then-edit pair. ⛔ `cur` is re-initialised rather than trusted: a failed
        //   read may have deposited a partial record in it (device_nv.h's §nv-ritual warning).
        if (st == mrnv::JoinRead::absent) mrnv::join_blob_init(cur);

        mrnv::JoinBlob want = cur;
        join_profile_put(want.prof[slot1 - 1], req, name, name_len);
        return commit(cur, want);
    }

    // `joinprofile clear <1..4>` — empty ONE slot of a VALID record.
    // ⛔⛔ IT MUST NEVER REPAIR A CORRUPT STORE, and this is the sharpest rule in the slice: `clear` would have to
    //    rewrite three slots it could not read, silently destroying them while reporting success. Recovery is
    //    `reset confirm` and nothing else.
    ProfileResult clear(long slot1) {
        ProfileResult r{};
        if (!valid_profile_slot(slot1)) { r.err = ProfileErr::bad_index; return r; }   // ⛔ 0 loads, 0 writes
        mrnv::JoinBlob cur{};
        const mrnv::JoinRead st = _store.load(cur);
        if (join_read_unreadable(st)) { r.err = profile_err_of_unreadable(st); return r; }   // ⛔⛔ NOT a repair
        // ★ ABSENT: nothing to clear, so NO CHANGE and ⛔ ZERO WRITES — and it is a SUCCESS, not an error. Seeding an
        //   empty record here would write flash to express "there was nothing there", which is both wear and a lie.
        if (st == mrnv::JoinRead::absent) { r.verdict = ProfileVerdict::empty; return r; }
        mrnv::JoinBlob want = cur;
        want.prof[slot1 - 1] = mrnv::JoinProfile{};      // zeroed => present == 0 => empty, by construction
        return commit(cur, want);                        // an already-empty slot compares equal -> `unchanged`, 0 writes
    }

    // `joinprofile reset confirm` — replace the record WHOLESALE with a valid empty one.
    // ★★ THE ONLY RECOVERY PATH FOR A CORRUPT STORE, which is exactly why it takes a `confirm`: it discards all four
    //    slots. ⛔ A missing confirm refuses WITHOUT LOADING and WITHOUT WRITING.
    ProfileResult reset(bool confirmed) {
        ProfileResult r{};
        if (!confirmed) { r.err = ProfileErr::needs_confirm; return r; }               // ⛔ 0 loads, 0 writes
        mrnv::JoinBlob cur{};
        const mrnv::JoinRead st = _store.load(cur);
        // ★★★ THE ONE PLACE THE TWO UNREADABLE ANSWERS PART. `invalid` is this verb's whole purpose — the record is
        //     wrong and it is rewritten (below). `io_failed` is NOT: the store could not be opened, so NOTHING is
        //     known about the record, and forcing a write here would discard four possibly-intact profiles because
        //     a mount failed transiently. ⇒ REFUSE, ⛔ ZERO writes. Recovering a store that will not open is a
        //     DEVICE-level job (`mount_or_repair` at boot, `factory_reset confirm`), ⛔ never this verb's.
        if (st == mrnv::JoinRead::io_failed) { r.err = ProfileErr::store_io_failed; return r; }   // ⛔ 0 writes
        // ★ ABSENT: already empty ⇒ ZERO WRITES, and an honest non-error. ⛔ Not "reset failed", and not a write.
        if (st == mrnv::JoinRead::absent) { r.verdict = ProfileVerdict::empty; return r; }
        mrnv::JoinBlob want{};
        mrnv::join_blob_init(want);
        // ⛔ A CORRUPT `cur` MUST NOT REACH THE BYTE COMPARE: its bytes are meaningless, so an accidental match would
        //    report `unchanged` and leave the store corrupt — the one path on which a zero-write answer would be the
        //    dishonest one. ⇒ corrupt writes UNCONDITIONALLY.
        if (st == mrnv::JoinRead::invalid) return commit_forced(want);
        return commit(cur, want);                        // an already-empty valid record -> `unchanged`, 0 writes
    }

  private:
    // ★ THE ONE WRITE DECISION, SPELLED ONCE (U1): compare the whole record, write at most once, and report the two
    //   zero-write successes distinctly from the one-write success.
    ProfileResult commit(const mrnv::JoinBlob& cur, const mrnv::JoinBlob& want) {
        ProfileResult r{};
        if (memcmp(&want, &cur, sizeof want) == 0) { r.verdict = ProfileVerdict::unchanged; return r; }
        return commit_forced(want);
    }
    ProfileResult commit_forced(const mrnv::JoinBlob& want) {
        ProfileResult r{};
        if (!_store.save(want)) { r.verdict = ProfileVerdict::nv_failed; r.err = ProfileErr::nv_save_failed; return r; }
        r.verdict = ProfileVerdict::ok;
        return r;
    }

    IJoinStore& _store;
};

}  // namespace mrfw
