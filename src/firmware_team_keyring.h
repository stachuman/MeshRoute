// MeshRoute — src/firmware_team_keyring.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-16 slice K1+K2 — THE `/mrteams` TEAM-KEY KEYRING. Spec:
// docs/superpowers/specs/2026-08-22-ui16-nearby-onboarding-spec.md §4-K1 / §4-K2 (owner-ruled 2026-08-22);
// register row [[B240]] (`docs/2026-07-30-open-bug-register.md`), which this slice IS the fix for.
//
// ★★★ WHAT WAS BROKEN: `Node::team_key_grant_receive` adopts a sealed team CONTENT key into RAM and pushes; the
//     firmware's push handler PRINTED and nothing else; and the only writers of `Blob::team_ch_*` are reached from
//     `seed_blob_from_live` on OTHER verbs' save paths. ⇒ a member who received the team key over the air could read
//     the encrypted channel until the first power-cycle and then silently could not — works at the trailhead, dead at
//     the summit. This file is where the key becomes durable.
//
// ★★ WHY THE DECISIONS ARE HERE AND NOT IN THE VERB / IN `fw_main`, for the third time in this arc and for the same
//    measured reason: `src/firmware_config.cpp` and `src/fw_main.cpp` are compiled by NEITHER the native suite
//    (`test_build_src = no`) NOR the simulator, and no corpus scenario runs a console verb or a boot. ⇒ a write-count
//    or a restore rule decided there would be unreachable by EVERY automated gate. This header is pure — no `Print`,
//    no Arduino, no globals — so `test/test_firmware_team_keyring.cpp` can COUNT the writes and DRIVE the restore.
//
// ★★★ THE POLICY, AS RULED (spec §4-K1), each clause pinned by a case and attacked by a mutation:
//        · `team_id == 0` is NEVER stored              · EXACTLY ONE record per `team_id`
//        · a re-grant / re-key REPLACES that team's record ATOMICALLY (one write, in place)
//        · IDENTICAL MATERIAL WRITES NOTHING           — the flash-wear guard, COUNTED, ⛔ never argued
//        · a FULL keyring FAILS LOUDLY (`KEYRING FULL`) and ⛔ NEVER silently evicts a secret (P-15)
//        · factory reset ERASES `/mrteams`             — delivered by the `"mr"` namespace, see `kSlotTeams`
//        · ⛔ NO LABELS in the record                  — there is no team-label store anywhere, and this is not one
//        · TEMPORARY SECRET BUFFERS ARE WIPED          — the `TeamRequest::wipe()` / `apply_team` scope-guard idiom
//        · boot install ONLY after an EXACT match — and the match is **FIVE TERMS** (P-2b, and QG's two
//          corrections of 2026-08-22): active · membership == binding · a record for that team · that record's pub
//          == the pub `/mrcfg` COMMITTED · the pub derives from the stored priv. ★ And the verdict GOVERNS: every
//          non-installing arm leaves the node KEYLESS by clearing, ⛔ never by declining to act.
//     ⚠ P-15 IS THE ONE PLACE WHERE "EVICT THE OLDEST", THE IDIOM USED EVERYWHERE ELSE IN THIS TREE (`peer_rec_put`,
//       the inbox rings, the route table), IS **WRONG**. A team content key is UNRECOVERABLE — ⛔ no seed derives it
//       (`lib/core/node_role.h:89`) — so evicting one destroys a secret that only a re-grant from a teammate can
//       restore. The pin exists because a reviewer's reflex is to reach for the idiom.
//
// ★ WHAT IT REUSES RATHER THAN FORKS (U1), verified at the declaration (V1):
//     · the RECORD and its four-state read: `mrnv::TeamKeyBlob` / `mrnv::TeamKeyRead` (`src/device_nv.h`), which in
//       turn reuse `SlotIo` / `slot_size_ok` / `blob_valid_exact` — one version+length+backend policy;
//     · the per-record byte-compare write guard: `mrnv::peer_rec_put`'s shape, and the `unchanged ⇒ ZERO writes`
//       verdict: `mrfw::ProfileVerdict`'s (`src/firmware_join_profiles.h:84-90`);
//     · the LIVE install: ⛔ NO new core accessor was invented. `ITeamKeyLive::adopt_key` binds to the EXISTING
//       `Node::team_channel_key_adopt(pub, priv)` (`lib/core/node.h:228`), which DERIVES the public half from the
//       private one and REFUSES on mismatch or on a degenerate scalar — i.e. the corruption rejection this store
//       needs already exists and is CALLED, ⛔ never re-derived here.
//
// ⛔⛔ SECRET HYGIENE, AND IT IS ABSOLUTE ON EVERY PATH BELOW: ⛔ no key byte is ever printed, echoed, logged or
//    placed in an error message. The verdicts name FACTS ("full", "the record does not verify"), ⛔ never material.
//    Every transient blob this file holds is `crypto_wipe`d by a SCOPE GUARD on EVERY exit — a hand-written wipe per
//    `return` is exactly the shape that gets one path added later and missed (`apply_team`'s own reason,
//    `src/firmware_provisioning_service.h:238-243`).
// ⓘ The transient blob is 296 B ON THE STACK and deliberately ⛔ NOT `static`: the console-frame lesson (`static Blob
//   cur` in `mrnv::save`) trades stack for a resident buffer, which for a SECRET means it outlives the call in .bss
//   where nothing wipes it. Wiped stack beats resident secret; the size is stated so the trade is visible.
//
// ⛔⛔ THE LIMIT OF EVERY CLAIM BELOW, in the words §PROV-TX and the `/mrjoin` store both had to use: these properties
//    are proved against a FAKE store that counts calls. ⛔ No NVS/LittleFS write, no flash WEAR and NO
//    reset-during-write behaviour is exercised ([[B193]]) — a `save` that reports failure may have written PARTIALLY,
//    so ⛔ nothing here or in its console voice may say "no flash was changed". The keyring's power-cut behaviour is
//    METAL-ONLY (M2) and is owed as a bench part.
//
// ⓘ WHAT THIS FILE DELIBERATELY DOES **NOT** HAVE YET (per [[meshroute-mark-done-vs-missing-in-code]]):
//     · ⛔ NO reader that hands key MATERIAL out. K5 (`SAVED KEY FOUND`) needs a PRESENCE question — "is there a
//       record for this team id?" — which is not key material; it is not added here because K5 is its own slice and
//       an accessor with no caller is untested surface.
//     · ⛔ NO `FORGET KEY` remover — owner-named as a future verb (spec string S-31), ⛔ not in this spec.
//     · ⛔ NO grant-receive persistence path — that is K3, which routes the `team_key_received` push through a
//       persistence function BEFORE the UI sees it (spec §4-K3 / F-10). K3 calls `put()` below; it adds no policy.
#pragma once
#include <cstdint>
#include <cstring>        // memcmp/memcpy — the byte-identical write guard and the one composition path
#include "device_nv.h"    // mrnv::TeamKeyBlob / TeamKeyRecord / TeamKeyRead — THE durable carrier (U2)
#include "monocypher.h"   // crypto_wipe — the SAME primitive Node::team_channel_key_* and TeamPlan::wipe use

namespace mrfw {

// ---- the durable seam ------------------------------------------------------------------------------------------
// ★ A THIRD store interface is not a fork of `ICfgStore` / `IJoinStore` (U1 was checked first): a different record,
//   and — like `IJoinStore` and ⛔ unlike `ICfgStore` — a FOUR-valued read whose whole purpose is to keep absent,
//   corrupt and unreadable apart. Widening either existing seam would change `/mrcfg`'s or `/mrjoin`'s behaviour
//   inside a storage slice (C1).
struct ITeamKeyStore {
    virtual ~ITeamKeyStore() = default;
    virtual mrnv::TeamKeyRead load(mrnv::TeamKeyBlob& out) = 0;
    virtual bool save(const mrnv::TeamKeyBlob& b) = 0;   // false = THE WRITE FAILED. ⛔ Never "nothing was written".
};
// ★★ THE LIVE SEAM, and its SHAPE carries two rules. **TWO operations, and the pair is the point** (⛔ CORRECTED
//    2026-08-22 — this said "ONE operation" before the governance half existed):
//      · `adopt_key` is FALLIBLE BY REQUIREMENT — the device binding is `Node::team_channel_key_adopt(pub, priv)`,
//        which re-derives the public half from the private one and REFUSES when the stored pub disagrees or the
//        scalar is degenerate. ⛔ It is NOT `team_channel_key_load`, the `void` verbatim primitive `/mrcfg` used:
//        loading verbatim installs a CORRUPTED pair as if it were a key, which is what term (v) forbids;
//      · `clear_key` is how a REFUSAL is APPLIED rather than merely reported (see below). Without it this seam could
//        only ever ADD a key, and a service that can only add cannot be the authority over what is live.
struct ITeamKeyLive {
    virtual ~ITeamKeyLive() = default;
    virtual bool adopt_key(const uint8_t pub[32], const uint8_t priv[32]) = 0;   // false = REFUSED; live state untouched
    // ★★★ THE GOVERNANCE HALF (QG blocker 1, 2026-08-22). ⛔ WITHDRAWN SHAPE, KEPT VISIBLE: the first cut had
    //     `adopt_key` alone, so a restore that could NOT install simply returned — and whatever a previous boot step
    //     had installed STAYED LIVE. On device that previous step was `/mrcfg`'s v22 `team_channel_key_load`, so an
    //     absent / corrupt / mismatched keyring left the OLD key running while this service reported it had not
    //     restored one. ⇒ the keyring's verdict must GOVERN, which takes a way to say "and therefore: no key".
    // ⓘ It binds to the EXISTING `Node::team_channel_key_clear()` (`lib/core/node.h:234`, `node.cpp:153`) — a
    //   `crypto_wipe` of both halves — ⛔ no new core accessor. Idempotent: clearing a keyless node is a no-op.
    virtual void clear_key() = 0;
};

// ---- what the BOOT restore is given, and every field is a TERM of the exact match ------------------------------
// ★★★ FIVE TERMS, ⛔ NOT TWO (QG blockers 2 and 3, 2026-08-22). ⛔ WITHDRAWN SHAPE, KEPT VISIBLE: the first cut took
//     `(active_team_id, key_active)` and matched on those alone, which let two real states install a key:
//       · ⛔ **a stale/damaged binding** — nothing compared the binding against the MEMBERSHIP `team_id`, so a
//         binding naming team B on a node that is in team A installed **B's key** (blocker 3);
//       · ⛔ **a FAILED same-team re-key** — the transaction replaces team A's KEYRING record and then fails its
//         `/mrcfg` write; the verb reports failure and applies nothing, but the binding for A is still active and
//         still points at A, so the next BOOT installed the key of a command the operator was told had FAILED
//         (blocker 2). A reboot must never make a failed request effective.
// ★ THE FIX USES NO NEW NV FIELD: `/mrcfg` already stores the COMMITTED public half (`Blob::team_ch_pub` +
//   `team_ch_key_present`, v22), and that record is written by the SAME transaction, LAST. ⇒ it is the witness that
//   the key in the keyring is the key the transaction actually committed. A failed re-key leaves the two disagreeing
//   and the node boots KEYLESS instead of silently adopting the failed request.
// ⓘ The pub is taken as a POINTER: it is the PUBLIC half, so there is no secret to copy, and a null pointer with
//   `committed_present` false is the honest "the record carries no key".
struct TeamKeyBinding {
    uint32_t       membership_team_id = 0;        // `/mrcfg` team_id — the team this node IS IN
    uint32_t       binding_team_id    = 0;        // `/mrcfg` team_key_team_id — the team the ACTIVE key is for
    bool           key_active         = false;    // `/mrcfg` team_key_active
    bool           committed_present  = false;    // `/mrcfg` team_ch_key_present
    const uint8_t* committed_pub      = nullptr;  // `/mrcfg` team_ch_pub — the COMMITTED public half
};

// ---- explicit outcomes -----------------------------------------------------------------------------------------
// ⛔⛔ THERE IS NO `empty` ARM, AND ITS ABSENCE IS A DECISION — ★ CORRECTED 2026-08-22 (QG). The first cut carried
//    `/mrjoin`'s `empty` (an ABSENT store, zero writes) because the precedent has one. **It had no producer here and
//    could not have one**, and the contract says why: an ABSENT store on `put` is SEEDED and written ONCE (the seed
//    and the record land in the same write), and an ABSENT store on `restore` is `KeyringRestore::no_record` — a
//    different enum. ⇒ the arm was removed rather than left to look like coverage, which is the defect class this
//    codebase has registered twice; the `-Wswitch` reader (`keyring_verdict_name`) and the all-enumerators case were
//    swept with it. ⓘ If a future READ verb ever needs "there is no store at all", it belongs in ITS OWN outcome
//    type, as `KeyringRestore::no_record` already is.
enum class KeyringVerdict : uint8_t {
    ok,          // the verb applied and performed EXACTLY ONE write
    unchanged,   // the stored record already matched, byte for byte -> ★ ZERO writes (the flash-wear guard)
    refused,     // a policy / corrupt-store refusal: ⛔ ZERO writes, ⛔ nothing evicted
    nv_failed,   // the ONE save attempt failed
};
enum class KeyringErr : uint8_t {
    none,
    zero_team,       // `team_id == 0` — ⛔ never stored (see TeamKeyRecord::team_id)
    // ★★★ P-15's ARM. A fifth team on a full keyring FAILS LOUDLY: ⛔ zero writes, ⛔ no record replaced, ⛔ nothing
    //     evicted. The operator-facing lexeme is `kKeyringFullText` below, owner-ruled (spec string S-30).
    keyring_full,
    store_invalid,   // the record is present but unreadable — the flash ate an UNRECOVERABLE secret; a teammate must re-grant
    // ⛔ NOT A SYNONYM FOR `store_invalid`, and the difference is what the operator can do about it: `store_invalid`
    //    says THE RECORD is wrong; `store_io_failed` says THE STORE COULD NOT BE OPENED, so nothing is known about
    //    the record and a blind rewrite would destroy up to four intact keys because a mount failed transiently.
    store_io_failed,
    nv_save_failed,  // carried by the nv_failed verdict
};
// enum -> string, `default`-LESS so `-Werror=switch` fails the build when an arm is added. Same discipline and the
// same reason as `mrnv::peer_put_name` / `mrfw::profile_err_name`: this project has shipped THREE enum->string
// defects that the byte-identity gate was structurally blind to.
inline const char* keyring_verdict_name(KeyringVerdict v) {
    switch (v) {
        case KeyringVerdict::ok:        return "ok";
        case KeyringVerdict::unchanged: return "unchanged";
        case KeyringVerdict::refused:   return "refused";
        case KeyringVerdict::nv_failed: return "nv_failed";
    }
    return "?";     // total function; -Werror=switch fires before this is reachable for a valid enumerator
}
inline const char* keyring_err_name(KeyringErr e) {
    switch (e) {
        case KeyringErr::none:            return "none";
        case KeyringErr::zero_team:       return "zero_team";
        case KeyringErr::keyring_full:    return "keyring_full";
        case KeyringErr::store_invalid:   return "store_invalid";
        case KeyringErr::store_io_failed: return "store_io_failed";
        case KeyringErr::nv_save_failed:  return "nv_save_failed";
    }
    return "?";
}
struct KeyringResult {
    KeyringVerdict verdict = KeyringVerdict::refused;
    KeyringErr     err     = KeyringErr::none;
};

// ★ THE ONE OPERATOR-FACING LEXEME THIS SLICE DECLARES (spec string S-30, owner-ruled 2026-08-22), spelled ONCE in a
//   pure unit and pinned by a native case, so an owner re-ruling changes it in exactly one place. 12 columns — it
//   fits the panel's 19 when K3/K5 come to render it. ⛔ It names a FACT about the store; it carries no material.
inline constexpr const char* kKeyringFullText = "KEYRING FULL";

// ---- the BOOT RESTORE's outcome --------------------------------------------------------------------------------
// ★ SEPARATE FROM `KeyringVerdict` ON PURPOSE: every arm here performs ZERO writes and ZERO loads-that-could-write,
//   so folding it into a verdict whose vocabulary is about WRITING would invite exactly one reader to believe a
//   restore can persist something. It cannot, on any path.
// ★★ ⛔ EXACTLY ONE ARM INSTALLS A KEY; EVERY OTHER ARM LEAVES THE NODE **KEYLESS** — and leaves it keyless
//    ACTIVELY, by calling `clear_key()`, ⛔ not by declining to act (QG blocker 1: declining is what let a
//    previously-installed key survive a refusal).
enum class KeyringRestore : uint8_t {
    installed,     // ★ ALL FIVE TERMS held and the node ADOPTED the record (pub re-derived + cross-checked)
    no_binding,    // (i) ⛔ NOT AN ERROR: /mrcfg carries no ACTIVE binding — a fresh device, or a node that ran
                   //     `team 0`. ★ ZERO loads: a retained record is not even looked at, which IS P-2b in code.
    // (ii) ★★★ THE BINDING DOES NOT NAME THE TEAM WE ARE IN (QG blocker 3). A binding is two facts — "a key is
    //      active" and "for which team" — and neither implies MEMBERSHIP. A stale or damaged binding naming
    //      another team must install NOTHING, ⛔ least of all that other team's key.
    team_mismatch,
    no_record,     // (iii) the binding names a team the keyring holds no record for -> KEYLESS
    // (iv) ★★★ THE KEYRING'S KEY IS NOT THE ONE `/mrcfg` COMMITTED (QG blocker 2). The keyring is written FIRST and
    //      `/mrcfg` LAST, so a transaction that failed in between leaves the keyring holding a key the committed
    //      record never witnessed. ⛔ A command reported as FAILED must not become effective at the next boot.
    not_committed,
    // (v) ★★ the stored `pub` does not match the one DERIVED from `priv` (or the scalar is degenerate) ⇒ the record
    //     is CORRUPT and the node comes up KEYLESS. ⛔ Never "install the private half anyway": a pair whose halves
    //     disagree can only come from corruption or a foreign convention, and a key nobody else holds seals posts
    //     nobody can read while the panel says the team is fine.
    rejected,
    store_failed,  // the store is corrupt / would not open -> KEYLESS, ⛔ zero writes, and the operator is told
};
inline const char* keyring_restore_name(KeyringRestore r) {
    switch (r) {
        case KeyringRestore::installed:     return "installed";
        case KeyringRestore::no_binding:    return "no_binding";
        case KeyringRestore::team_mismatch: return "team_mismatch";
        case KeyringRestore::no_record:     return "no_record";
        case KeyringRestore::not_committed: return "not_committed";
        case KeyringRestore::rejected:      return "rejected";
        case KeyringRestore::store_failed:  return "store_failed";
    }
    return "?";
}

// ---- the two UNREADABLE answers, named once (U1) ----------------------------------------------------------------
// ⛔ `absent` IS NOT UNREADABLE. A fresh device read its store perfectly; there was simply nothing in it.
inline bool team_key_read_unreadable(mrnv::TeamKeyRead st) {
    return st == mrnv::TeamKeyRead::invalid || st == mrnv::TeamKeyRead::io_failed;
}
// The 1:1 relabel. ⛔ Never collapse the two: the record being wrong and the store being unreachable take different
// operator actions, and telling him the wrong one costs him up to four unrecoverable keys.
inline KeyringErr keyring_err_of_unreadable(mrnv::TeamKeyRead st) {
    return st == mrnv::TeamKeyRead::io_failed ? KeyringErr::store_io_failed : KeyringErr::store_invalid;
}

// ---- the SECRET WIPE, as a NAMED SCOPE GUARD (§3.10's discipline, made measurable) ------------------------------
// ★★ THE IDIOM IS `apply_team`'s AND THE REASON IS ITS REASON (`src/firmware_provisioning_service.h:693-696`): a
//    hand-written `crypto_wipe` before each `return` is exactly the shape that gets one path added later and missed.
//    ⇒ ONE guard, declared WITH its carrier, wiping on EVERY exit — success, refusal and early return alike.
// ★★★ AND IT IS A NAMED TYPE RATHER THAN A LOCAL `struct` PER USE SITE **SO THAT A HOST TEST CAN DRIVE IT**: a
//     wiped STACK FRAME cannot be inspected after the call without reading dangling storage (UB), so the wipe inside
//     a service body is unobservable — the class of claim this codebase calls "seventeen green instruments". Hoisted,
//     `test/test_firmware_team_keyring.cpp` constructs a filled carrier, scopes a guard over it and asserts the bytes
//     are ZERO afterwards, which is fully-defined behaviour and a mutation can redden.
// ⛔ `crypto_wipe`, ⛔ never `memset`: a plain store loop over a buffer nothing reads afterwards is precisely what a
//    compiler is entitled to elide (the reason `Node::team_channel_key_clear` gives for the same choice).
template <typename T>
struct SecretWipeGuard {
    T& ref;
    ~SecretWipeGuard() { crypto_wipe(&ref, sizeof(T)); }
};

// ---- the ONE composition path (U2 — ⛔ never rebuild the carrier field-by-field at a second site) ----------------
// ★ THE RECORD IS ZEROED FIRST, WHOLE. That is not tidiness: `reserved` and every byte of both halves must be
//   DETERMINISTIC, or the byte-identical write guard below would fire on indeterminate padding and rewrite flash —
//   of a SECRET — for nothing. (The same reasoning `peer_rec_merge` states for `PeerRec`.)
// ⛔ NO LABEL, and it is a ruling rather than an omission: there is no team-label field anywhere in this codebase,
//    the T-K3 grant's optional `name=` is explicitly not persisted (`lib/core/node.cpp:264-266`), and the keyring
//    ruling repeats it. A label here would be the first team-name store, invented inside a storage slice.
inline void team_key_rec_put(mrnv::TeamKeyRecord& r, uint32_t team_id,
                             const uint8_t pub[32], const uint8_t priv[32]) {
    r = mrnv::TeamKeyRecord{};
    r.team_id = team_id;
    memcpy(r.team_ch_pub,  pub,  sizeof r.team_ch_pub);
    memcpy(r.team_ch_priv, priv, sizeof r.team_ch_priv);
}
// EXACTLY ONE RECORD PER `team_id` is enforced by this lookup being the only way in: `put` either finds the team's
// row and replaces it in place, or appends. ⛔ Returns an index into `rec[]`, ⛔ never a pointer — a pointer into a
// blob that is about to be wiped is a dangling secret waiting for a later edit.
// ⚠ BOUNDED BY `count`, AND IT CLAMPS FOR ITSELF: a bit-rotted count must never index past `rec[]` — the same guard
//   `peer_rec_put` opens with. `team_key_clamp_count` below is the WRITE-side half (it keeps the corruption from
//   being persisted forward); this one is the READ-side half, and neither relies on the other having run.
inline int team_key_find(const mrnv::TeamKeyBlob& b, uint32_t team_id) {
    if (team_id == 0) return -1;                      // ⛔ 0 is never stored, so it can never be FOUND either
    const uint16_t n = (b.count > mrnv::kTeamKeyRecs) ? mrnv::kTeamKeyRecs : b.count;
    for (uint16_t i = 0; i < n; ++i) if (b.rec[i].team_id == team_id) return static_cast<int>(i);
    return -1;
}
inline void team_key_clamp_count(mrnv::TeamKeyBlob& b) {
    if (b.count > mrnv::kTeamKeyRecs) b.count = mrnv::kTeamKeyRecs;
}

// ---- the service -------------------------------------------------------------------------------------------
// RAM: one reference. ⛔ No cached record and no draft — each verb is a single load-edit-store transaction, so there
// is no state between calls to go stale, ⛔ and no member holding a secret between calls.
class TeamKeyringService {
  public:
    explicit TeamKeyringService(ITeamKeyStore& store) : _store(store) {}

    // STORE (or replace) one team's content key. The caller is `team new` / an import / — from K3 — a received
    // grant; all of them hand over material they have ALREADY validated, which is why this takes bytes and not a
    // request struct.
    // ★★★ THE ORDER IS THE CONTRACT: refuse id 0 -> load -> refuse an unreadable store -> seed an absent one ->
    //     compose ONE candidate record -> byte compare -> AT MOST ONE save. A zero id costs ⛔ ZERO loads and ZERO
    //     writes; an unreadable store costs ZERO writes; identical material costs ⛔ ZERO writes; a FULL keyring
    //     costs ZERO writes and evicts NOTHING; ★ an ABSENT store costs EXACTLY ONE (the seed and the record land in
    //     the same write, ⛔ never two).
    KeyringResult put(uint32_t team_id, const uint8_t pub[32], const uint8_t priv[32]) {
        KeyringResult r{};
        if (team_id == 0) { r.err = KeyringErr::zero_team; return r; }     // ⛔ 0 loads, 0 writes

        mrnv::TeamKeyBlob cur{};
        // ⚠ SECRET-BEARING FROM THE NEXT LINE ON. One guard, declared WITH its carrier, covering every exit below.
        SecretWipeGuard<mrnv::TeamKeyBlob> cguard{cur};
        const mrnv::TeamKeyRead st = _store.load(cur);
        if (team_key_read_unreadable(st)) { r.err = keyring_err_of_unreadable(st); return r; }   // ⛔ 0 writes
        // ★ THE SEED. An absent store becomes a valid EMPTY record IN RAM and the edited record is what gets
        //   written — one write, not a seed-then-edit pair. ⛔ `cur` is re-initialised rather than trusted: a failed
        //   read may have deposited a partial record in it (device_nv.h's §nv-ritual warning).
        if (st == mrnv::TeamKeyRead::absent) mrnv::team_key_blob_init(cur);
        team_key_clamp_count(cur);

        mrnv::TeamKeyRecord want{};
        SecretWipeGuard<mrnv::TeamKeyRecord> rguard{want};
        team_key_rec_put(want, team_id, pub, priv);

        const int idx = team_key_find(cur, team_id);
        if (idx >= 0) {
            // ★★ THE FLASH-WEAR GUARD, AND IT IS A WHOLE-RECORD COMPARE — which is what `reserved[4]` being a NAMED
            //    member buys (indeterminate padding would make this compare answer differently on the same material).
            //    ⓘ Re-granting the SAME key is the common case, not an edge one: a teammate re-sends on every join.
            if (memcmp(&want, &cur.rec[idx], sizeof want) == 0) {
                r.verdict = KeyringVerdict::unchanged;                     // ★ ZERO writes
                return r;
            }
            cur.rec[idx] = want;      // ★ ATOMIC REPLACE of THAT team's record — a re-key overwrites in place, and
                                      //   ⛔ never appends a second row for one team_id
        } else {
            // ★★★ P-15. ⛔ NO EVICTION. Not the oldest, not the least-recently-used, not "the one whose team we left"
            //     — a stored team key is UNRECOVERABLE, and the only honest answer when there is no room is to refuse
            //     and say so. ⛔ ZERO writes, ⛔ nothing replaced.
            if (cur.count >= mrnv::kTeamKeyRecs) { r.err = KeyringErr::keyring_full; return r; }
            cur.rec[cur.count++] = want;
        }
        if (!_store.save(cur)) { r.verdict = KeyringVerdict::nv_failed; r.err = KeyringErr::nv_save_failed; return r; }
        r.verdict = KeyringVerdict::ok;
        return r;
    }

    // ★★★ THE BOOT RESTORE, AND IT IS THE **ONE AUTHORITY** OVER THE LIVE TEAM CONTENT KEY (QG blocker 1).
    //     ⛔ `/mrcfg`'s v22 copy no longer installs itself at boot: it is passed in HERE as the committed WITNESS
    //     (term iv), and nothing else calls `team_channel_key_load` on the startup path. That is what makes this
    //     function's verdict govern instead of merely follow.
    // ★★ THE EXACT MATCH IS FIVE TERMS, each of which is a way a node could otherwise come up holding a key it must
    //    not hold — see `TeamKeyBinding` for the two the first cut was missing and the states that produced them:
    //      (i)   the binding is ACTIVE                     — `team 0` clears it while RETAINING the record (P-2b)
    //      (ii)  the binding names the team we are IN      — a stale binding must not install another team's key
    //      (iii) the keyring holds a record for that team  — otherwise KEYLESS, ⛔ never a substitute
    //      (iv)  that record's pub == the COMMITTED pub    — a half-finished transaction must not become effective
    //      (v)   the pub derives from the stored priv      — corruption is rejected, ⛔ never adopted
    // ★ ZERO WRITES ON EVERY PATH, ZERO LOADS when there is no binding, and ⛔ **EVERY non-installing path CLEARS
    //   THE LIVE KEY** — the verdict is applied, not merely reported.
    KeyringRestore restore(const TeamKeyBinding& bind, ITeamKeyLive& live) {
        // (i) ★ BOTH HALVES, and neither is redundant: `key_active` is what `team 0` clears while RETAINING the
        //     record, and the non-zero id keeps a corrupt `key_active=1, id=0` blob from reaching the lookup.
        if (!bind.key_active || bind.binding_team_id == 0) return refuse(live, KeyringRestore::no_binding);  // ⛔ 0 loads

        // (ii) ★★★ MEMBERSHIP, COMPARED EXPLICITLY (QG blocker 3). ⛔ The binding is NOT self-authorising: it says a
        //      key is active and for whom, and neither fact establishes that this node is in that team. A binding
        //      that has gone stale against `/mrcfg`'s `team_id` installs NOTHING.
        if (bind.membership_team_id != bind.binding_team_id) return refuse(live, KeyringRestore::team_mismatch);

        mrnv::TeamKeyBlob cur{};
        SecretWipeGuard<mrnv::TeamKeyBlob> cguard{cur};
        const mrnv::TeamKeyRead st = _store.load(cur);
        if (st == mrnv::TeamKeyRead::absent) return refuse(live, KeyringRestore::no_record);
        if (st != mrnv::TeamKeyRead::ok)     return refuse(live, KeyringRestore::store_failed);   // invalid / io_failed
        team_key_clamp_count(cur);

        // (iii) A record for ANOTHER team is RETAINED and ⛔ NOT installed — the node comes up keyless rather than
        //       sealing its posts under a key its current team cannot read.
        const int idx = team_key_find(cur, bind.binding_team_id);
        if (idx < 0) return refuse(live, KeyringRestore::no_record);

        // (iv) ★★★ THE COMMITTED WITNESS (QG blocker 2). `/mrcfg` is written LAST and carries the public half the
        //      transaction actually committed; the keyring is written FIRST. ⇒ a re-key whose `/mrcfg` write FAILED
        //      leaves the two disagreeing, and the honest answer at the next boot is KEYLESS — ⛔ never the key of a
        //      command the operator was told had failed. ⓘ A `/mrcfg` that carries NO key witnesses nothing.
        if (!bind.committed_present || !bind.committed_pub
            || memcmp(cur.rec[idx].team_ch_pub, bind.committed_pub, 32) != 0)
            return refuse(live, KeyringRestore::not_committed);

        // (v) ★★ THE CORRUPTION CHECK IS THE INSTALL. `adopt_key` re-derives the public half from the private one
        //     and refuses when the stored pub disagrees — so a record that does not verify installs NOTHING.
        //     ⛔ There is deliberately no fallback that loads the pair verbatim.
        if (!live.adopt_key(cur.rec[idx].team_ch_pub, cur.rec[idx].team_ch_priv))
            return refuse(live, KeyringRestore::rejected);
        return KeyringRestore::installed;
    }

  private:
    // ★★★ THE GOVERNANCE, SPELLED ONCE (U1) — QG blocker 1's fix, and the shape matters: every non-installing return
    //     of `restore` goes through this, so a refusal arm added later CANNOT forget to leave the node keyless. A
    //     hand-written `live.clear_key();` before each of the six returns is exactly the shape that gets a seventh
    //     added and missed (the reason `apply_team` uses a scope guard for its wipe).
    // ⛔ It is NOT "clear if a key is present": the seam is idempotent and the service must not hold a belief about
    //    live state it did not establish.
    static KeyringRestore refuse(ITeamKeyLive& live, KeyringRestore why) {
        live.clear_key();
        return why;
    }

    ITeamKeyStore& _store;
};

}  // namespace mrfw
