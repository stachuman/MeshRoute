// MeshRoute — src/firmware_admin_keyring.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ REMOTE-ADMIN v2 SLICE 4 — the CONTROLLER-SIDE MANAGEMENT KEYRING (`/mrmkeys`), as a PURE service.
//
// Authority: design `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` §§6.2-6.4;
// rulings R-RA-29 (the frozen fingerprint) and R-RA-30 (the controller/BLE split). Design §6.2, verbatim:
//
//     "The controller has exactly ten dedicated management slots, independent of the target's ten ACL slots. Each
//      occupied local slot persists only one 32-byte master seed. Firmware derives the full `Identity` through
//      `identity_from_seed()` when needed, uses the existing conversion/ECDH path, and wipes transient expanded
//      secret material when the operation no longer needs it."
//
// WHAT IT IS. TEN independent 32-byte MASTER SEEDS in one NV record, addressed by the `key0`…`key9` grammar. Six
// operations — list, show, generate, import, export, remove — plus a confirm-gated `reset` of a CORRUPT record and
// a READ-ONLY boot report. Nothing else.
//
// ★★★ WHY IT IS A PURE HEADER AND NOT A `.cpp`: `platformio.ini`'s native env sets `test_build_src = no`, so NO
//     native or simulator build compiles a single `src/*.cpp`. A policy decided inside `firmware_commands.cpp`
//     would be a rule no automated gate could fail — the shape this codebase calls "seventeen green instruments".
//     Hoisted, `test/test_firmware_admin_keyring.cpp` drives every arm against fake stores that COUNT reads,
//     writes and entropy draws, and `--target=radmin4key` can attack every one of them.
// ⛔ THERE IS NO CAPABILITY MACRO IN THIS FILE ([[B255]] idiom). `MR_FEAT_RADMIN_CLIENT` gates the INSTANTIATION
//    and the console surfacing in `firmware_commands.cpp` / `fw_main.cpp` / `firmware_help.h`, so the native suite
//    exercises every service arm without defining a product role, and an ACCEPT board compiles the header but
//    instantiates nothing.
//
// ⓘ WHAT THIS FILE DELIBERATELY DOES **NOT** HAVE (per [[meshroute-mark-done-vs-missing-in-code]]):
//   · ⛔ NO RESIDENT KEYRING, SEED OR EXPANDED IDENTITY, and ⛔ no `Node` member. Every operation loads into a
//     GUARDED STACK TRANSIENT, derives what it needs, renders PUBLIC material through its caller's sink and wipes.
//     ⛔ The 2056-byte resident scratch Slice 4 does pay belongs to the PUBLIC target book, ⛔ never to this file.
//   · ⛔ NO SESSION, EPOCH, RPC OR CODEC CALL — Slices 5/6/8a's. This slice emits ZERO remote events, which is why
//     the 36-scenario corpus is inert by construction.
//   · ⛔ NO WIRING INTO `/mradmid`. That record is the seed a TARGET is administered BY; this one holds the seeds
//     this CONTROLLER administers OTHERS with, and design §6.4 forbids either crossing into the other.
//   · ⛔ NO `/mrid` CONTACT AND NO ROTATION OF IT. `self` below is the node's own messaging identity, READ ONLY:
//     `regen` rotates that key and must never touch a management seed (Slice 4's regen note says so on the console).
//
// ⚠⚠ THE LIMIT OF EVERY CLAIM HERE, stated so no console line can over-promise (design §6.4, [[B317]]):
//   · A save that reports FAILURE publishes no success and installs nothing — and it does ⛔ NOT promise the
//     previous flash bytes survived. `mrnv::write_slot` on nRF52 is `remove()` THEN `open/write`, so a failed or
//     power-cut write can leave the record ABSENT. The next command re-reads and re-classifies. ⛔ No journal,
//     witness file, rollback write or automatic retry.
//   · A whole-filesystem self-heal (`mount_or_repair`) triggered by one of the SIX OTHER probed files erases this
//     record too. That is [[B317]], it is NOT closed here, and ordinary `regen`/`leave` preservation is a
//     different and much weaker statement than preservation across a reformat.
//   · The entropy seam's `true` means ONLY "a complete, non-zero 32-byte draw arrived". It is ⛔ NOT a device-RNG
//     health guarantee: `mrrng::fill` returns `void`, can block, and cannot expose every failure mode. [[B312]]
//     stays OPEN for the truthful first-RF entropy integration; ⛔ nothing here closes it.
//   · "A non-zero seed" is a DEGENERACY refusal, ⛔ not an entropy-quality claim: this service cannot tell a
//     hardware draw from an operator pasting `0101…01`, and it does not pretend to.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>                  // memcmp/memcpy — the byte-identical write guard and the one copy path

#include "device_nv.h"               // mrnv::MgmtKeyBlob / MgmtKeyRow / MgmtKeyRead — THE durable carrier (U2)
#include "firmware_admin_identity.h" // ★ mrfw::admin_buf_all_zero + IAdminSeedSource + SecretWipeGuard — REUSED,
                                     //   ⛔ never re-declared, re-implemented or moved (U1)
#include "identity.h"                // meshroute::Identity + identity_from_seed — the ONE derivation authority

namespace mrfw {

// ---- the CONTENT policy (the `/mrui` split: LAYOUT is device_nv.h's, CONTENT is here) -----------------------
// ★ AN OCCUPIED ROW IS ONE WITH A NON-ZERO SEED, and that is the whole occupancy rule — there is no separate flag
//   byte to disagree with the material. The all-zero seed is exactly what a dead RNG produces (`mrrng::fill` on the
//   HOST writes ZEROS by design, `src/device_rng.h:44`), so it is reserved for EMPTY and can never be minted into.
inline bool mgmt_key_row_occupied(const mrnv::MgmtKeyRow& r) { return !admin_buf_all_zero(r.seed, sizeof r.seed); }

// ★★★ EVERY RULE THE RECORD MUST OBEY, IN ONE PREDICATE — and a record that breaks ANY of them is INVALID rather
//     than partially adopted. ⛔ NEVER clamp a count, compact a row or drop an offending entry: a half-understood
//     keyring is a set of secrets nobody reviewed, and the answer to a corrupt one is the confirm-gated
//     `admin-key reset confirm`, not a silent repair.
//       · `count` is the POPULATION and must equal it exactly — ⛔ not a high-water mark and ⛔ not an index. Holes
//         are legal and keep their slot numbers (`key3` stays `key3` after `key2` is removed).
//       · `reserved` must be ZERO on EVERY row, occupied or not. The write-coalescing policy is a WHOLE-RECORD
//         `memcmp`, so a record carrying a stray reserved byte would compare unequal to the canonically-written
//         same keyring and rewrite flash — of SECRETS — for ever. (`AdminIdBlob::reserved`'s rule.)
//       · SEEDS ARE PAIRWISE UNIQUE. Two identical seeds are two slots holding ONE principal, which is the stored
//         form of the duplicate the mint path refuses. ⓘ ⛔ THIS IS DELIBERATELY THE **SEED** COMPARISON AND NOT
//         TEN Ed25519 KEYGENS: `identity_from_seed` is a keygen, so deriving ten identities on every read (i.e. on
//         every `list`, every `show`, every mutation and every boot) would be an unbounded cost on a record whose
//         validity must be decidable cheaply. Distinct seeds are what distinct derived identities are STORED as;
//         the derived-identity comparison — the one design §6.2 words — is enforced where a NEW key enters, in
//         `mint_()` below, against self and every other slot.
inline bool mgmt_key_content_valid(const mrnv::MgmtKeyBlob& b) {
    if (b.count > mrnv::kMgmtKeySlots) return false;
    uint16_t occupied = 0;
    for (uint8_t i = 0; i < mrnv::kMgmtKeySlots; ++i) {
        const mrnv::MgmtKeyRow& r = b.rec[i];
        if (!admin_buf_all_zero(r.reserved, sizeof r.reserved)) return false;
        if (!mgmt_key_row_occupied(r)) continue;
        ++occupied;
        for (uint8_t j = 0; j < i; ++j)
            if (mgmt_key_row_occupied(b.rec[j]) && !memcmp(b.rec[j].seed, r.seed, sizeof r.seed)) return false;
    }
    return b.count == occupied;
}

// ---- the composed state: storage classification AND content policy ------------------------------------------
enum class MgmtKeyState : uint8_t {
    ok,        // a valid record — possibly EMPTY, which is an ordinary un-provisioned controller
    absent,    // ★ NO RECORD — ⛔ never an error
    invalid,   // ⛔ present but unusable: bad storage bytes, or content that breaks a rule above
    io_failed, // ⛔ the STORE would not answer — ⛔ NOTHING may be written, not even the recovery verb
};
inline MgmtKeyState mgmt_key_state_of(mrnv::MgmtKeyRead r, const mrnv::MgmtKeyBlob& b) {
    switch (r) {
        case mrnv::MgmtKeyRead::io_failed: return MgmtKeyState::io_failed;
        case mrnv::MgmtKeyRead::absent:    return MgmtKeyState::absent;
        case mrnv::MgmtKeyRead::invalid:   return MgmtKeyState::invalid;
        case mrnv::MgmtKeyRead::ok:        break;
    }
    return mgmt_key_content_valid(b) ? MgmtKeyState::ok : MgmtKeyState::invalid;
}

// ---- the durable seam --------------------------------------------------------------------------------------
// ★ A store interface of its own is not a fork of `IAdminIdStore` (U1 was checked first): a different record and a
//   different capacity. Widening the target-side seam would make one type address two records on two product
//   roles, which is exactly the crossing design §6.4 forbids.
struct IMgmtKeyStore {
    virtual ~IMgmtKeyStore() = default;
    virtual mrnv::MgmtKeyRead load(mrnv::MgmtKeyBlob& out) = 0;
    virtual bool save(const mrnv::MgmtKeyBlob& b) = 0;   // false = THE WRITE FAILED. ⛔ Never "nothing was written".
};

// ---- the FUTURE-CALLER in-use predicate ---------------------------------------------------------------------
// ★★ Design §6.4's "never invents an active remote owner", applied to removal: a key an OPEN SESSION or a pending
//    request is authenticating with must not be deleted underneath it. ⛔ Slice 4 has NO producer for that fact —
//    Slice 8a is the first — so the PRODUCTION binding answers "nothing is in use", and every test that supplies a
//    busy predicate is labelled a **future-caller service test**, ⛔ never an executed live-RPC test.
// ⛔ IT IS A SEAM, ⛔ NOT A RUNTIME FLAG OR AN OVERRIDABLE FEATURE MACRO: a `#define` here would be a second
//    authority for a fact the session layer owns.
struct IMgmtKeyUse {
    virtual ~IMgmtKeyUse() = default;
    virtual bool slot_in_use(uint8_t slot) const = 0;   // this ONE slot authenticates something live
    virtual bool any_in_use() const = 0;                // ANY slot does — the bulk `reset`'s gate
};

// ---- the typed verdicts ------------------------------------------------------------------------------------
// ⓘ EXHAUSTIVE AND MINIMAL: every reason below is REACHED by a native case. ⛔ No unreachable enumerator is added
//   to round out a symmetry — an unreachable reason is an untestable claim. In particular there is ⛔ NO `full`:
//   slots are addressed EXPLICITLY (`key0`…`key9`), so nothing here allocates and nothing can run out.
enum class MgmtKeyErr : uint8_t {
    none,             // success carries this; a refusal never does
    bad_args,         // ⛔ a subcommand, selector or extra token this family does not accept. The VERB layer
                      //    produces it; the service produces it only for an out-of-range slot index.
    needs_confirm,    // ⛔ a destructive verb reached without its EXACT `confirm` token — ZERO writes, ZERO draws
    store_invalid,    // the record is present and corrupt — only the confirm-gated reset may touch it
    store_io_failed,  // ⛔ the store would not answer: NOTHING is known, so NOTHING may be written
    not_invalid,      // reset attempted on a record that is not corrupt (absent or ok)
    not_found,        // the named slot holds no key (show / export / remove)
    occupied,         // generate/import into a slot that already holds one — ⛔ never a silent replacement
    duplicate,        // ★ the derived public identity is already this node's own, or another slot's
    bad_material,     // ⛔ an all-zero imported seed — the world-known degenerate root, refused (C2)
    entropy_failed,   // ⛔ the draw did not complete, or completed all-zero — ⛔ never minted from
    in_use,           // ⛔ a live caller authenticates with this key (or with some key, for reset)
    nv_save_failed,   // the durable write reported failure. ⛔ Does NOT promise the old bytes survived.
};
// ★ PUBLIC MATERIAL ONLY. The seed NEVER appears here; `ed_pub` is populated on success alone (a refusal leaves it
//   all-zero, which every caller can see). The ONE exception is `MgmtKeySeed` below, which exists precisely so the
//   secret-bearing result is a DIFFERENT TYPE that a reader cannot confuse with the ordinary one.
struct MgmtKeyResult {
    bool       ok   = false;
    MgmtKeyErr err  = MgmtKeyErr::none;
    uint8_t    slot = 0;
    uint8_t    ed_pub[32] = {};
};
// ⚠⚠ THE ONE SECRET-BEARING RESULT, AND IT IS A SEPARATE TYPE ON PURPOSE (`admin-key export`, USB-only per design
//    §6.2). Its caller MUST scope a `SecretWipeGuard` over it; the verb in `firmware_admin_client_verbs.h` does.
struct MgmtKeySeed {
    bool       ok   = false;
    MgmtKeyErr err  = MgmtKeyErr::none;
    uint8_t    slot = 0;
    uint8_t    seed[32] = {};
};
// The `admin-key list` payload — PUBLIC material for every slot, derived once from ONE load.
struct MgmtKeyListRow { bool occupied = false; uint8_t ed_pub[32] = {}; };
struct MgmtKeyList {
    bool       ok    = false;
    MgmtKeyErr err   = MgmtKeyErr::none;
    uint8_t    count = 0;
    MgmtKeyListRow rec[mrnv::kMgmtKeySlots];
};
// ---- the read-only BOOT report -------------------------------------------------------------------------------
// ★ It VALIDATES AND REPORTS. ⛔ It writes nothing, generates nothing, derives no key and prints no key byte.
// ⚠ For every NON-ok state `count` is ZERO and it means "NO ACCEPTED KEYS" — ⛔ never "the flash is empty". The
//   state carries that distinction, which is why the console prints it FIRST and never omits it.
struct MgmtKeyBoot {
    MgmtKeyState state = MgmtKeyState::absent;
    uint8_t      count = 0;
};

// ---- the service ---------------------------------------------------------------------------------------------
// RAM: three references and a 32-byte PUBLIC copy of this node's own identity. ⛔ No cached record, no draft and no
// secret member between calls — there is no state here to go stale, which is what makes "load, derive, render,
// wipe" honest rather than aspirational.
// ★ `self_pub` IS AN INPUT, ⛔ not something this file reads: the node's messaging identity lives in `/mrid` and is
//   already resident as `g_identity`, and a second reader of it here would be a fork of that authority (U1).
class MgmtKeyService {
  public:
    MgmtKeyService(IMgmtKeyStore& store, IAdminSeedSource& seed, const IMgmtKeyUse& use,
                   const uint8_t self_pub[32])
        : _store(store), _seed(seed), _use(use) {
        memcpy(_self_pub, self_pub, sizeof _self_pub);
    }

    // Read-only classification. ONE load, ZERO writes, on every arm.
    MgmtKeyState state() {
        mrnv::MgmtKeyBlob b{};
        SecretWipeGuard<mrnv::MgmtKeyBlob> g{b};   // ⚠ a non-ok read may leave PARTIAL SEED BYTES in `b`
        return classify_(b);
    }
    MgmtKeyBoot boot_report() {
        mrnv::MgmtKeyBlob b{};
        SecretWipeGuard<mrnv::MgmtKeyBlob> g{b};
        MgmtKeyBoot r;
        r.state = classify_(b);
        if (r.state == MgmtKeyState::ok) r.count = population_(b);
        return r;
    }

    // ★ `admin-key show self` — THIS NODE'S OWN messaging identity, and it needs NO store read at all. It is
    //   therefore the one arm that still answers while the keyring is corrupt or unreadable (brief §4.3), because
    //   the fact it reports does not live in that record.
    MgmtKeyResult show_self() const {
        MgmtKeyResult r;
        r.ok = true;
        memcpy(r.ed_pub, _self_pub, sizeof r.ed_pub);
        return r;
    }

    // `admin-key show keyN` — PUBLIC material from an occupied slot. ZERO writes on every arm.
    MgmtKeyResult show(uint8_t slot) {
        if (slot >= mrnv::kMgmtKeySlots) return fail_(MgmtKeyErr::bad_args);
        mrnv::MgmtKeyBlob b{};
        SecretWipeGuard<mrnv::MgmtKeyBlob> g{b};
        const MgmtKeyState s = classify_(b);
        if (s != MgmtKeyState::ok) return fail_(state_err_(s));
        if (!mgmt_key_row_occupied(b.rec[slot])) return fail_(MgmtKeyErr::not_found);
        MgmtKeyResult r;
        r.ok = true;
        r.slot = slot;
        pub_of_(b.rec[slot].seed, r.ed_pub);
        return r;
    }

    // ★ `admin-key list` — one load, every occupied slot's PUBLIC identity, then wipe. ⛔ The listing REFUSES on a
    //   non-ok state rather than printing an empty keyring: "unreadable" and "empty" are different facts and the
    //   operator acts differently on each. (`show self` above stays available, deliberately.)
    MgmtKeyList list() {
        mrnv::MgmtKeyBlob b{};
        SecretWipeGuard<mrnv::MgmtKeyBlob> g{b};
        MgmtKeyList out;
        const MgmtKeyState s = classify_(b);
        if (s != MgmtKeyState::ok) { out.err = state_err_(s); return out; }
        out.ok = true;
        for (uint8_t i = 0; i < mrnv::kMgmtKeySlots; ++i) {
            if (!mgmt_key_row_occupied(b.rec[i])) continue;
            out.rec[i].occupied = true;
            pub_of_(b.rec[i].seed, out.rec[i].ed_pub);
            ++out.count;
        }
        return out;
    }

    // ★ `admin-key generate keyN` — a fresh CHECKED draw into a VACANT slot. ⛔ Never a replacement: an occupied
    //   slot is `occupied` and the operator must `remove … confirm` first, deliberately, because a management seed
    //   silently overwritten is a set of targets that will never accept this controller again.
    MgmtKeyResult generate(uint8_t slot) { return mint_(slot, nullptr); }
    // ★ `admin-key import keyN <64hex>` — the same path with the seed SUPPLIED. ⛔ The borrowed buffer is the
    //   CALLER's and is not wiped here (the verb owns it); this service copies it into its own guarded candidate.
    MgmtKeyResult import(uint8_t slot, const uint8_t seed[32]) { return mint_(slot, seed); }

    // ⚠⚠ `admin-key export keyN` — THE ONE ARM THAT RETURNS A SECRET, USB-only by R-RA-30's split (enforced by the
    //    BLE guard, not here: this file is transport-blind). ZERO writes.
    MgmtKeySeed export_seed(uint8_t slot) {
        MgmtKeySeed out;
        if (slot >= mrnv::kMgmtKeySlots) { out.err = MgmtKeyErr::bad_args; return out; }
        mrnv::MgmtKeyBlob b{};
        SecretWipeGuard<mrnv::MgmtKeyBlob> g{b};
        const MgmtKeyState s = classify_(b);
        if (s != MgmtKeyState::ok) { out.err = state_err_(s); return out; }
        if (!mgmt_key_row_occupied(b.rec[slot])) { out.err = MgmtKeyErr::not_found; return out; }
        out.ok = true;
        out.slot = slot;
        memcpy(out.seed, b.rec[slot].seed, sizeof out.seed);
        return out;
    }

    // `admin-key remove keyN confirm` — free one slot, leaving a HOLE. ⛔ Never compacted: `key7` keeps its name.
    MgmtKeyResult remove(uint8_t slot) {
        if (slot >= mrnv::kMgmtKeySlots) return fail_(MgmtKeyErr::bad_args);
        if (_use.slot_in_use(slot))      return fail_(MgmtKeyErr::in_use);   // ⛔ BEFORE any load (zero reads too)
        mrnv::MgmtKeyBlob b{};
        SecretWipeGuard<mrnv::MgmtKeyBlob> g{b};
        const MgmtKeyState s = classify_(b);
        if (s != MgmtKeyState::ok) return fail_(state_err_(s));
        if (!mgmt_key_row_occupied(b.rec[slot])) return fail_(MgmtKeyErr::not_found);
        const uint16_t pop = population_(b);
        b.rec[slot] = mrnv::MgmtKeyRow{};                      // ⛔ ZEROED IN PLACE — never compacted
        b.count = static_cast<uint16_t>(pop - 1);
        if (!mgmt_key_content_valid(b)) return fail_(MgmtKeyErr::store_invalid);   // the composed-bytes belt
        if (!_store.save(b))            return fail_(MgmtKeyErr::nv_save_failed);
        MgmtKeyResult r;
        r.ok = true;
        r.slot = slot;
        return r;
    }

    // ★★ `admin-key reset confirm` — the ONLY write permitted over an INVALID record, and the sole exception to
    //    "an unreadable store costs zero writes". ⛔ `io_failed` refuses even here: nothing is known, so a re-init
    //    would destroy up to ten intact master seeds because a mount failed transiently. ⛔ `absent`/`ok` are
    //    `not_invalid` — there is nothing to recover, and this verb is ⛔ NOT a bulk-delete escape from `remove`.
    MgmtKeyResult recover() {
        if (_use.any_in_use()) return fail_(MgmtKeyErr::in_use);
        mrnv::MgmtKeyBlob b{};
        SecretWipeGuard<mrnv::MgmtKeyBlob> g{b};
        const MgmtKeyState s = classify_(b);
        if (s == MgmtKeyState::io_failed) return fail_(MgmtKeyErr::store_io_failed);
        if (s != MgmtKeyState::invalid)   return fail_(MgmtKeyErr::not_invalid);
        mrnv::MgmtKeyBlob cand{};
        SecretWipeGuard<mrnv::MgmtKeyBlob> gc{cand};   // ⛔ empty by construction, guarded anyway (one rule, no exceptions)
        mrnv::mgmt_key_blob_init(cand);
        if (!_store.save(cand)) return fail_(MgmtKeyErr::nv_save_failed);
        MgmtKeyResult r;
        r.ok = true;
        return r;
    }

  private:
    MgmtKeyState classify_(mrnv::MgmtKeyBlob& b) { return mgmt_key_state_of(_store.load(b), b); }
    static uint16_t population_(const mrnv::MgmtKeyBlob& b) {
        uint16_t n = 0;
        for (uint8_t i = 0; i < mrnv::kMgmtKeySlots; ++i) if (mgmt_key_row_occupied(b.rec[i])) ++n;
        return n;
    }
    static MgmtKeyResult fail_(MgmtKeyErr e) { MgmtKeyResult r; r.err = e; return r; }
    // ⛔ NO `default:` — `-Werror=switch` must refuse a new state nobody named.
    static MgmtKeyErr state_err_(MgmtKeyState s) {
        switch (s) {
            case MgmtKeyState::io_failed: return MgmtKeyErr::store_io_failed;
            case MgmtKeyState::invalid:   return MgmtKeyErr::store_invalid;
            case MgmtKeyState::absent:    return MgmtKeyErr::not_found;   // no record ⇒ no such key
            case MgmtKeyState::ok:        break;
        }
        return MgmtKeyErr::none;
    }

    // ★★★ THE ONE MINT PATH (U2) — generate and import both reach it, so there is ⛔ no second place a seed can be
    //     drawn, validated, de-duplicated or saved. THE ORDER IS THE CONTRACT:
    //       range -> load -> refuse an unreadable/corrupt store -> seed an ABSENT record in RAM -> refuse an
    //       OCCUPIED slot -> obtain material (draw or the caller's) -> refuse a DEAD/degenerate seed -> DERIVE ->
    //       refuse a duplicate of self or of another slot -> compose -> validate -> AT MOST ONE save -> publish.
    //     ⇒ every refusal costs ZERO writes; a refused draw emits ⛔ no key; a failed save publishes ⛔ no success.
    // ★ THE DUPLICATE CHECK IS OVER **DERIVED PUBLIC IDENTITIES**, which is design §6.2's own wording, and it
    //   includes THIS NODE'S OWN key: a controller whose management key equals its messaging key would make the two
    //   authorities indistinguishable to every target.
    MgmtKeyResult mint_(uint8_t slot, const uint8_t supplied[32]) {
        if (slot >= mrnv::kMgmtKeySlots) return fail_(MgmtKeyErr::bad_args);
        mrnv::MgmtKeyBlob b{};
        SecretWipeGuard<mrnv::MgmtKeyBlob> g{b};       // ⚠ carries SEEDS on every path out, refusals included
        const MgmtKeyState s = classify_(b);
        if (s == MgmtKeyState::io_failed) return fail_(MgmtKeyErr::store_io_failed);
        if (s == MgmtKeyState::invalid)   return fail_(MgmtKeyErr::store_invalid);
        if (s == MgmtKeyState::absent)    mrnv::mgmt_key_blob_init(b);   // seed in RAM — ONE write, below
        if (mgmt_key_row_occupied(b.rec[slot])) return fail_(MgmtKeyErr::occupied);

        mrnv::MgmtKeyRow row{};
        SecretWipeGuard<mrnv::MgmtKeyRow> gr{row};
        if (supplied) {
            memcpy(row.seed, supplied, sizeof row.seed);
            if (admin_buf_all_zero(row.seed, sizeof row.seed)) return fail_(MgmtKeyErr::bad_material);
        } else {
            if (!_seed.fill(row.seed))                          return fail_(MgmtKeyErr::entropy_failed);
            if (admin_buf_all_zero(row.seed, sizeof row.seed))  return fail_(MgmtKeyErr::entropy_failed);
        }

        uint8_t cand_pub[32];
        pub_of_(row.seed, cand_pub);
        if (!memcmp(cand_pub, _self_pub, sizeof cand_pub)) return fail_(MgmtKeyErr::duplicate);
        for (uint8_t i = 0; i < mrnv::kMgmtKeySlots; ++i) {
            if (i == slot || !mgmt_key_row_occupied(b.rec[i])) continue;
            uint8_t other[32];
            pub_of_(b.rec[i].seed, other);
            if (!memcmp(other, cand_pub, sizeof other)) return fail_(MgmtKeyErr::duplicate);
        }

        const uint16_t pop = population_(b);
        b.rec[slot] = row;                                     // ⛔ `reserved` rides along ZEROED, deterministically
        b.count = static_cast<uint16_t>(pop + 1);
        if (!mgmt_key_content_valid(b)) return fail_(MgmtKeyErr::store_invalid);   // the composed-bytes belt
        if (!_store.save(b))            return fail_(MgmtKeyErr::nv_save_failed);
        MgmtKeyResult r;
        r.ok = true;
        r.slot = slot;
        memcpy(r.ed_pub, cand_pub, sizeof r.ed_pub);
        return r;
    }

    // ★ The expanded `Identity` is 196 B of SECRET on the stack for the duration of one derivation and is wiped by
    //   the guard on every exit. ⛔ It is never returned, never stored and never a member. (`AdminIdService`'s
    //   `pub_of_`, deliberately the same shape and the same reason — ⛔ it is NOT callable from here: that one is a
    //   private static of another class in another header.)
    static void pub_of_(const uint8_t seed[32], uint8_t out_pub[32]) {
        meshroute::Identity id{};
        SecretWipeGuard<meshroute::Identity> g{id};
        meshroute::identity_from_seed(id, seed);
        memcpy(out_pub, id.ed_pub, 32);
    }

    IMgmtKeyStore&    _store;
    IAdminSeedSource& _seed;
    const IMgmtKeyUse& _use;
    uint8_t           _self_pub[32] = {};   // PUBLIC material — ⛔ no secret member exists in this class
};

}  // namespace mrfw
