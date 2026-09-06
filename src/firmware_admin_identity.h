// MeshRoute — src/firmware_admin_identity.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ REMOTE-ADMIN v2 SLICE 3 — the TARGET-SIDE ADMINISTRATION IDENTITY (`/mradmid`), as a PURE service.
//
// Authority: design `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` §§6.2-6.4;
// rulings R-RA-6 (the four stores in the `/mrteams` keyring idiom), R-RA-8 (accept = static + gateway),
// R-RA-21 (the `physical` authority class), R-RA-29 (the frozen fingerprint).
//
// WHAT IT IS. One 32-byte SEED, persisted in its own NV record, from which `meshroute::identity_from_seed`
// derives the whole 196-byte expanded `Identity` at the moment one is needed. Four operations — show, generate,
// rotate, recover — plus a READ-ONLY boot report. Nothing else.
//
// ★★★ WHY IT IS A PURE HEADER AND NOT A `.cpp`: `platformio.ini`'s native env sets `test_build_src = no`, so NO
//     native or simulator build compiles a single `src/*.cpp`. A policy decided inside `firmware_commands.cpp`
//     would be a rule no automated gate could fail — the shape this codebase calls "seventeen green instruments".
//     Hoisted, `test/test_firmware_admin_identity.cpp` drives every arm against fake stores that COUNT reads,
//     writes and entropy draws, and `--target=radmin3id` can attack every one of them.
// ⛔ THERE IS NO CAPABILITY MACRO IN THIS FILE. `MR_FEAT_RADMIN_ACCEPT` gates the INSTANTIATION and the console
//    surfacing in `firmware_commands.cpp` / `fw_main.cpp` / `firmware_help.h` — the [[B255]] idiom — so the native
//    suite exercises every service arm without defining a product role, and a CLIENT board compiles the header
//    but instantiates nothing.
//
// ⓘ WHAT THIS FILE DELIBERATELY DOES **NOT** HAVE (per [[meshroute-mark-done-vs-missing-in-code]]):
//   · ⛔ NO RESIDENT IDENTITY, SEED OR CACHE, and ⛔ no `Node` member. Every operation loads, derives, renders
//     PUBLIC material through its caller's sink and wipes. Design §6.2's residency question is SLICE 5's, with
//     its own RAM attribution (C1): an authenticated request needs ECDH per call, and whether that is paid as a
//     per-request Ed25519 keygen or as 196 B of secret in `.bss` is a decision this slice does not pre-empt.
//   · ⛔ NO SESSION, EPOCH OR INVALIDATION — the session slice's. ⛔ NO `CommandContext`, no remote caller
//     authority and no role check against a caller — Slice 6's. ⛔ NO codec call and no remote execution: this
//     slice adds ZERO remote events, which is why the 36-scenario corpus is inert by construction.
//   · ⛔ NO WIRING INTO THE LEGACY SINGLE-ADMIN STATE (`Blob::admin_pubkey`, `Node::admin_load`, `g_admin_id`,
//     `password`/`unlock`/`lock`, `remote_exec`). Slice 10 removes that, in its own NV-version slice.
//   · ⛔ NO `/mrid` CONTACT. The node's messaging identity and this administration root are separate secrets with
//     separate lifetimes; `regen` rotates the former and must never rotate the latter.
//
// ⚠⚠ THE LIMIT OF EVERY CLAIM HERE, stated so no console line can over-promise (design §6.4, [[B317]]):
//   · A save that reports FAILURE publishes no success and installs nothing — and it does ⛔ NOT promise the
//     previous flash bytes survived. `mrnv::write_slot` on nRF52 is `remove()` THEN `open/write`, so a failed or
//     power-cut write can leave the record ABSENT, and a "failed" write may even have reached the medium. The
//     next command re-reads and re-classifies. ⛔ No journal, witness file, rollback write or automatic retry.
//   · A whole-filesystem self-heal (`mount_or_repair`) triggered by one of the SIX OTHER probed files erases this
//     record too. That is [[B317]], it is NOT closed here, and ordinary `regen`/`leave` preservation is a
//     different and much weaker statement than preservation across a reformat.
//   · The entropy seam's `true` means ONLY "a complete, non-zero 32-byte draw arrived". It is ⛔ NOT a device-RNG
//     health guarantee: `mrrng::fill` returns `void`, can block, and cannot expose every failure mode. [[B312]]
//     stays OPEN for the truthful first-RF entropy integration; ⛔ nothing here closes it.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>                  // memcmp/memcpy — the byte-identical write guard and the one copy path

#include "device_nv.h"               // mrnv::AdminIdBlob / AdminIdRead — THE durable carrier (U2)
#include "firmware_team_keyring.h"   // ★ mrfw::SecretWipeGuard — REUSED, ⛔ never re-declared or moved (U1)
#include "identity.h"                // meshroute::Identity + identity_from_seed — the ONE derivation authority
#include "monocypher.h"              // crypto_blake2b (R-RA-29's digest) + crypto_wipe (through the guard)

namespace mrfw {

// ---- small byte predicates ---------------------------------------------------------------------------------
// ★ NOT a fork of `lib/core/identity.cpp`'s `all_zero32`: that one sits in an anonymous namespace inside a `.cpp`
//   (verified at `identity.cpp:52`), so it is TU-private and unreachable from here. U1 was checked first; there is
//   no public predicate to extend, and adding one to `lib/core` would be a core edit this slice is not allowed.
inline bool admin_buf_all_zero(const uint8_t* p, size_t n) {
    uint8_t acc = 0;
    for (size_t i = 0; i < n; ++i) acc = static_cast<uint8_t>(acc | p[i]);   // ⛔ no early exit: constant-time-ish
    return acc == 0;
}

// ---- R-RA-29: THE ONE FINGERPRINT OF AN ED25519 PUBLIC KEY --------------------------------------------------
// ★★★ THE RULING, VERBATIM: *"fp(ed_pub) = BLAKE2b-512(ed_pub)[:8], rendered as 16 lowercase hex characters."*
//     It is the ONE fingerprint everywhere remote-admin v2 shows or selects a key: this file's `admin-id show`,
//     the ACL listing, the USB first-owner exchange, and Slice 4's controller `/mrtargets` trust selector. ⇒ ONE
//     implementation, here, reused by both listings and available to Slice 4 ⛔ without a copy.
// ⛔ WHAT IT IS NOT, each spelled out because each is a plausible wrong turn:
//     · ⛔ NOT `key_hash32` (the first four key bytes, LE). `lib/core/identity.h:40` says of it in as many words
//       "NOT a security anchor"; it is a ROUTING hash and it is four bytes, i.e. grindable.
//     · ⛔ NOT an 8-byte-output BLAKE2 variant. BLAKE2b's output length is part of its parameter block, so
//       `crypto_blake2b(h, 8, …)` is a DIFFERENT function with a different digest — not a truncation of this one.
//       The ruling says 512 bits, then TAKE the first eight bytes. The reference vectors pin the difference.
//     · ⛔ NO little-endian integer conversion, NO domain-separation label, NO seed hashing, NO four-byte prefix.
//     · ⛔ NEVER a trust anchor by itself: it is a DISPLAY/SELECTION handle. The full 64-hex key is printed beside
//       it on USB precisely because the physical exchange copies the whole key.
// ⓘ The expected values are FROZEN INDEPENDENT LITERALS generated BEFORE this helper existed, by CPython's
//   `hashlib.blake2b(digest_size=64)` outside both repositories, itself anchored on the RFC 7693 Appendix A
//   known-answer vector for BLAKE2b-512("abc"). ⛔ No expected fingerprint in the suite was produced by this code.
inline constexpr size_t kAdminFpBytes  = 8;                    // ★ the PREFIX length, taken in digest order
inline constexpr size_t kAdminFpHex    = 2 * kAdminFpBytes;    // 16 lowercase hex characters
inline constexpr size_t kAdminKeyHex   = 64;                   // the FULL public key, 32 bytes, lowercase hex
inline constexpr size_t kAdminDigestB  = 64;                   // BLAKE2b-**512** — the ruling's digest size

// Lowercase hex, `2*n` characters plus a NUL. ⛔ No `%02x` / snprintf: this must be identical on all three ABIs
// and must never depend on a locale or on `int` promotion of a signed char.
inline void admin_hex_lower(const uint8_t* p, size_t n, char* out) {
    static const char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
        out[2 * i]     = kHex[(p[i] >> 4) & 0x0F];
        out[2 * i + 1] = kHex[p[i] & 0x0F];
    }
    out[2 * n] = '\0';
}
// `out` must hold kAdminFpHex + 1 bytes.
inline void admin_fp_hex(const uint8_t ed_pub[32], char* out) {
    uint8_t digest[kAdminDigestB];
    crypto_blake2b(digest, sizeof digest, ed_pub, 32);   // BLAKE2b-512 over EXACTLY the 32 public-key bytes
    admin_hex_lower(digest, kAdminFpBytes, out);         // …and the FIRST EIGHT bytes, in digest order
    // ⓘ The digest is PUBLIC material (it is a hash of a public key), so there is nothing to wipe here — said
    //   explicitly so its absence reads as a decision rather than as a missed guard.
}
// `out` must hold kAdminKeyHex + 1 bytes.
inline void admin_key_hex(const uint8_t ed_pub[32], char* out) { admin_hex_lower(ed_pub, 32, out); }

// ---- the CONTENT policy (the `/mrui` split: LAYOUT is device_nv.h's, CONTENT is here) -----------------------
// ★ An all-zero seed is INVALID, and it is the exact failure a dead RNG produces: `mrrng::fill` on the HOST writes
//   ZEROS by design (`src/device_rng.h:44`, "degenerate-on-purpose"), and `do_regen` has no such guard. A record
//   holding it would derive one fixed, world-known root on every such device. `lib/core/identity.cpp:52-66`
//   already refuses the analogous degenerate scalar for the team key; this is the same rule for this store (C2).
// ★ `reserved` must be ZERO. The write-coalescing policy below is a WHOLE-RECORD `memcmp`, so a record carrying
//   a stray reserved byte would compare unequal to the canonically-written same seed and rewrite flash — of a
//   SECRET — forever. (`UiPresetSlot::enabled`'s "exactly 0 or 1" ruling, one record over.)
inline bool admin_id_content_valid(const mrnv::AdminIdBlob& b) {
    if (b.reserved != 0) return false;
    return !admin_buf_all_zero(b.seed, sizeof b.seed);
}

// ---- the COMPOSED state: storage classification AND content policy ------------------------------------------
// ⓘ Four arms, the same four `mrnv::AdminIdRead` carries, ⛔ never collapsed. The composition is the only place
//   where "the bytes arrived but they are not a usable root" becomes `invalid` rather than `ok`.
enum class AdminIdState : uint8_t {
    ok,        // a valid record holding a non-zero seed
    absent,    // ★ NO RECORD — an un-provisioned node, ⛔ never an error
    invalid,   // ⛔ present but unusable: bad length/magic/version, a stray reserved byte, or an all-zero seed
    io_failed, // ⛔ the STORE would not answer — a fact about the DEVICE, over which NOTHING may be written
};
inline AdminIdState admin_id_state_of(mrnv::AdminIdRead r, const mrnv::AdminIdBlob& b) {
    switch (r) {
        case mrnv::AdminIdRead::io_failed: return AdminIdState::io_failed;
        case mrnv::AdminIdRead::absent:    return AdminIdState::absent;
        case mrnv::AdminIdRead::invalid:   return AdminIdState::invalid;
        case mrnv::AdminIdRead::ok:        break;
    }
    return admin_id_content_valid(b) ? AdminIdState::ok : AdminIdState::invalid;
}

// ---- the durable seam --------------------------------------------------------------------------------------
// ★ A store interface of its own is not a fork of `ICfgStore` / `IJoinStore` / `ITeamKeyStore` (U1 was checked
//   first): a different record, and — like the last two — a FOUR-valued read whose whole purpose is to keep
//   absent, corrupt and unreadable apart. Widening an existing seam would change a shipped record's behaviour
//   inside a feature slice (C1).
struct IAdminIdStore {
    virtual ~IAdminIdStore() = default;
    virtual mrnv::AdminIdRead load(mrnv::AdminIdBlob& out) = 0;
    virtual bool save(const mrnv::AdminIdBlob& b) = 0;   // false = THE WRITE FAILED. ⛔ Never "nothing was written".
};

// ---- the CHECKED entropy seam ([[B312]]'s idiom) ------------------------------------------------------------
// ★★★ WHY IT RETURNS A BOOL AT ALL. `mrrng::fill` is `void` (`src/device_rng.h:46`) and on the HOST it writes
//     ZEROS. A generate path built on it directly would mint the all-zero seed on every host build and on any
//     board whose RNG is dead, and would report SUCCESS. ⇒ the seam ANSWERS, and every refusal arm costs zero
//     writes and zero output.
// ⚠⚠ AND WHAT THE `true` MEANS, EXACTLY — the honesty this seam exists for: "32 bytes were written and they are
//    not all zero". It is ⛔ NOT a device-RNG health report and ⛔ NOT a hardware-entropy guarantee: a `void`
//    draw can block and cannot expose every failure mode, so [[B312]] stays OPEN. The device binding in
//    `firmware_commands.cpp` returns the ACTUAL non-zero check and ⛔ never an unconditional `true`.
struct IAdminSeedSource {
    virtual ~IAdminSeedSource() = default;
    virtual bool fill(uint8_t out[32]) = 0;
};

// ---- the typed verdicts ------------------------------------------------------------------------------------
// ⓘ EXHAUSTIVE AND MINIMAL: every reason below is REACHED by a native case. ⛔ No unreachable enumerator is added
//   to round out a symmetry — an unreachable reason is an untestable claim.
enum class AdminIdErr : uint8_t {
    none,             // success carries this; a refusal never does
    bad_args,         // ⛔ a subcommand, extra token or confirmation this family does not accept. The VERB layer
                      //    produces it; the service never does — it is a grammar verdict, not a store verdict.
    absent,           // rotate/show on a node that has no administration identity yet
    store_invalid,    // the record is present and corrupt — only the confirm-gated recovery may touch it
    store_io_failed,  // ⛔ the store would not answer: NOTHING is known, so NOTHING may be written
    already_present,  // generate on a node that already has a valid root (rotate is the verb that replaces one)
    not_invalid,      // recovery attempted on a record that is not corrupt (absent or ok)
    entropy_failed,   // ⛔ the draw did not complete, or completed all-zero — ⛔ never minted from
    nv_save_failed,   // the durable write reported failure. ⛔ Does NOT promise the old bytes survived.
};
struct AdminIdResult {
    bool       ok  = false;
    AdminIdErr err = AdminIdErr::none;
    // ★ PUBLIC MATERIAL ONLY. The seed NEVER leaves the service; this is the derived Ed25519 public key, and it is
    //   populated on success alone (a refusal leaves it all-zero, which every caller can see).
    uint8_t    ed_pub[32] = {};
};

// ---- the read-only BOOT report -------------------------------------------------------------------------------
// ★ It VALIDATES AND REPORTS. ⛔ It installs no identity, writes nothing, auto-generates nothing, derives no key
//   and prints no key bytes — design §6.4 forbids inventing an active owner, and a boot that mints a root would
//   do exactly that on the first transient read failure.
struct AdminIdBoot { AdminIdState state = AdminIdState::absent; };

// ---- the service ---------------------------------------------------------------------------------------------
// RAM: two references. ⛔ No cached record, no draft, no secret member between calls — there is no state here to
// go stale, which is what makes "load, derive, render, wipe" honest rather than aspirational.
class AdminIdService {
  public:
    AdminIdService(IAdminIdStore& store, IAdminSeedSource& seed) : _store(store), _seed(seed) {}

    // Read-only classification. ONE load, ZERO writes, on every arm.
    AdminIdState state() {
        mrnv::AdminIdBlob b{};
        SecretWipeGuard<mrnv::AdminIdBlob> g{b};   // ⚠ a non-ok read may leave PARTIAL SEED BYTES in `b`
        return classify_(b);
    }
    AdminIdBoot boot_report() { return AdminIdBoot{ state() }; }

    // `admin-id show` — PUBLIC material from a valid record. ZERO writes on every arm.
    AdminIdResult show() {
        mrnv::AdminIdBlob b{};
        SecretWipeGuard<mrnv::AdminIdBlob> g{b};
        const AdminIdState s = classify_(b);
        if (s != AdminIdState::ok) return fail_(state_err_(s, AdminIdErr::absent));
        AdminIdResult r;
        r.ok = true;
        pub_of_(b.seed, r.ed_pub);
        return r;
    }

    // ★ `admin-id generate` creates ONLY an ABSENT identity. An existing valid root is `already_present` (rotate
    //   is the verb that replaces one); an invalid or unreadable store refuses with ZERO writes and ZERO draws.
    AdminIdResult generate() {
        const AdminIdState s = state();
        if (s == AdminIdState::io_failed) return fail_(AdminIdErr::store_io_failed);
        if (s == AdminIdState::invalid)   return fail_(AdminIdErr::store_invalid);
        if (s == AdminIdState::ok)        return fail_(AdminIdErr::already_present);
        return mint_();
    }
    // ★ `admin-id rotate confirm` replaces ONLY an OK identity. ⛔ It does not erase the ACL and does not touch
    //   `/mrid`: the controllers listed in `/mracl` keep their rows, and every one of them must re-pin this node's
    //   NEW fingerprint physically — that is the cost of a rotation, and it is the design's.
    AdminIdResult rotate() {
        const AdminIdState s = state();
        if (s == AdminIdState::io_failed) return fail_(AdminIdErr::store_io_failed);
        if (s == AdminIdState::invalid)   return fail_(AdminIdErr::store_invalid);
        if (s == AdminIdState::absent)    return fail_(AdminIdErr::absent);
        return mint_();
    }
    // ★★ `admin-id reset confirm` recovers ONLY an INVALID record, and it is the SOLE exception to "an unreadable
    //    store costs zero writes". ⛔ `io_failed` refuses even here: nothing is known about the record, so a
    //    re-mint would destroy an intact root because a mount failed transiently. ⛔ `absent`/`ok` are
    //    `not_invalid` — there is nothing to recover, and `generate`/`rotate` are the verbs that own those states.
    AdminIdResult recover() {
        const AdminIdState s = state();
        if (s == AdminIdState::io_failed) return fail_(AdminIdErr::store_io_failed);
        if (s != AdminIdState::invalid)   return fail_(AdminIdErr::not_invalid);
        return mint_();
    }

  private:
    AdminIdState classify_(mrnv::AdminIdBlob& b) { return admin_id_state_of(_store.load(b), b); }

    static AdminIdResult fail_(AdminIdErr e) { AdminIdResult r; r.err = e; return r; }
    // The ok arm is the caller's business, so it is never passed here; `ok_repl` is the reason a valid record is
    // the WRONG state for the operation asking.
    static AdminIdErr state_err_(AdminIdState s, AdminIdErr ok_repl) {
        switch (s) {
            case AdminIdState::io_failed: return AdminIdErr::store_io_failed;
            case AdminIdState::invalid:   return AdminIdErr::store_invalid;
            case AdminIdState::absent:    return AdminIdErr::absent;
            case AdminIdState::ok:        break;
        }
        return ok_repl;
    }

    // ★★★ THE ONE MINT PATH (U2) — generate, rotate and recover all reach it, so there is ⛔ no second place a
    //     seed can be drawn, validated or saved. THE ORDER IS THE CONTRACT:
    //       draw -> refuse an incomplete draw -> refuse a DEAD (all-zero) draw -> AT MOST ONE save -> only THEN
    //       derive and publish the public key.
    //     ⇒ a refused draw costs ZERO writes and ⛔ emits no key; a failed save publishes ⛔ no success and
    //       derives nothing. `identity_from_seed` is an Ed25519 keygen, so doing it before the save would also be
    //       wasted work on the refusal paths — but the reason it is ordered this way is the honesty rule, not cost.
    AdminIdResult mint_() {
        mrnv::AdminIdBlob cand{};
        SecretWipeGuard<mrnv::AdminIdBlob> g{cand};       // ⚠ carries the SEED on every path out, refusals included
        mrnv::admin_id_blob_init(cand);                    // the ONE composition path — magic/version/reserved
        if (!_seed.fill(cand.seed))            return fail_(AdminIdErr::entropy_failed);   // provider said NO
        if (!admin_id_content_valid(cand))     return fail_(AdminIdErr::entropy_failed);   // a DEAD, all-zero draw
        if (!_store.save(cand))                return fail_(AdminIdErr::nv_save_failed);
        AdminIdResult r;
        r.ok = true;
        pub_of_(cand.seed, r.ed_pub);
        return r;
    }

    // ★ The expanded `Identity` is 196 B of SECRET on the stack for the duration of one derivation and is wiped by
    //   the guard on every exit. ⛔ It is never returned, never stored and never a member — see the residency note
    //   at the head of this file.
    static void pub_of_(const uint8_t seed[32], uint8_t out_pub[32]) {
        meshroute::Identity id{};
        SecretWipeGuard<meshroute::Identity> g{id};
        meshroute::identity_from_seed(id, seed);
        memcpy(out_pub, id.ed_pub, 32);
    }

    IAdminIdStore&    _store;
    IAdminSeedSource& _seed;
};

}  // namespace mrfw
