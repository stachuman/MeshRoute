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
//     · ✅ LANDED 2026-08-25 (§UI-16 K5) — the PRESENCE question and the EXPLICIT activation. ⛔ THIS ENTRY IS
//       CORRECTED IN PLACE RATHER THAN DELETED: it read *"⛔ NO reader that hands key MATERIAL out. K5
//       (`SAVED KEY FOUND`) needs a PRESENCE question — 'is there a record for this team id?' — which is not key
//       material; it is not added here because K5 is its own slice and an accessor with no caller is untested
//       surface."* ★ THE HALF THAT HELD IS THE ONE THAT MATTERED: `has_record()` below answers a **BOOLEAN** and
//       `use_saved()` hands the material to the **LIVE SEAM AND THE `/mrcfg` WRITER AND NOWHERE ELSE** — ⛔ there is
//       still no reader that returns key bytes to a caller, and there is still no caller that could print one.
//     · ✅ LANDED 2026-08-25 (§UI-16 K6) — the `FORGET KEY` remover and its metadata-only enumeration. ⛔ THIS ENTRY
//       IS CORRECTED IN PLACE RATHER THAN DELETED: it read *"⛔ NO `FORGET KEY` remover — owner-named as a future
//       verb (spec string S-31), ⛔ not in this spec."* Repeated real-team creation filled all four records on metal,
//       and retention without a removal path is not an operable lifecycle — so the owner RULED it (spec §4-K6).
//       ★ THE HALVES THAT HELD ARE THE ONES THAT MATTERED: `list()` returns `{team_id, active}` and ⛔ NO key bytes,
//       so there is STILL no reader that hands material out; and `put()` above is byte-for-byte unchanged — P-15's
//       loud `KEYRING FULL` still refuses a fifth team and still evicts NOTHING. ⛔ It is saved-key RETENTION
//       MANAGEMENT, ⛔ never key rotation, and the ACTIVE key is PROTECTED.
//     · ✅ LANDED 2026-08-24 (§UI-16 K3) — the GRANT-RECEIVE persistence path. ⛔ THIS ENTRY IS CORRECTED IN PLACE
//       RATHER THAN DELETED: it read *"⛔ NO grant-receive persistence path — that is K3 … K3 calls `put()` below;
//       it adds no policy."* The first half is now false; ★ the second half HELD — `TeamKeyGrantService` at the foot
//       of this file calls `put()` unchanged and adds ⛔ no write policy of its own. What it adds is the ORDER and
//       the FOUR HANDLING-TIME RE-CHECKS (spec §4-K3 / F-10), which are a different question.
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

// ---- §UI-16 K5 — THE **EXPLICIT** ACTIVATION's OUTCOME (spec §4-K5, P-2b) ----------------------------------------
// ★★★★ WHY THIS IS A THIRD OUTCOME TYPE AND ⛔ NOT `KeyringRestore` REUSED, checked before it was written (U1): the
//      restore's block one screen up states *"EVERY ARM PERFORMS ZERO WRITES"* and that is the property its whole
//      vocabulary is built on. ★ THIS VERB **WRITES** — it commits the `/mrcfg` ACTIVE BINDING — so folding it into
//      that enum would put a writing arm inside a type a reader has been told cannot persist anything. (It is the
//      same argument `KeyringRestore` itself makes for not being a `KeyringVerdict`.)
// ★★★ THE GOVERNANCE IS **NOT** THE BOOT RESTORE'S — ⛔ CORRECTED IN PLACE 2026-08-25 (QG blocker 1), AND THE
//     WITHDRAWN CONTRACT IS KEPT VISIBLE BECAUSE IT WAS NORMATIVE: this block read *"AND IT IS THE SAME
//     **GOVERNANCE**: ⛔ EXACTLY ONE ARM LEAVES A KEY LIVE. Every other arm goes through `refuse()` below, which
//     CLEARS — so a `USE SAVED KEY` that could not complete leaves the node **KEYLESS**"*. ⛔ **THAT RULE WAS WRONG
//     HERE, AND WRONG IN THE DANGEROUS DIRECTION**: the live key at this moment belongs to whatever team `/mrcfg`
//     currently NAMES, so clearing on a refusal destroys the CURRENT team's innocent key — see the SURGICAL block
//     below, which is what replaced it. ★ THE HALF THAT SURVIVED IS THE ONE THAT MATTERS: ⛔ never half-installed
//     (C2), and the RETAINED RECORD IS UNTOUCHED on every one of them.
// ⛔⛔ P-2b IS WHAT THIS VERB EXISTS TO OBEY, NOT TO WEAKEN: nothing here runs unless an operator pressed
//     `USE SAVED KEY` on the offer screen. Knowledge of the PUBLIC team id reaches `has_record()` — a BOOLEAN — and
//     stops there. ⛔ There is no path from a join, a beacon or a push to this function.
// ★★★★ THE REFUSAL IS **SURGICAL**, ⛔ NOT THE GLOBAL CLEARING FUNNEL — ADDED 2026-08-25 (QG blocker 1), and the
//      distinction is the whole safety argument of this verb: `refuse()` (the boot restore's) exists because THERE
//      the live key IS THE SUSPECT — a previous boot step may have installed one this node must not hold, so the
//      verdict has to be APPLIED by clearing. ⛔ HERE THE LIVE KEY IS INNOCENT: the offer is reached from a join
//      that left the node keyless, and if a live key exists at all it belongs to the team `/mrcfg` currently names —
//      which this verb has no mandate to destroy. ⇒ every refusing arm below returns DIRECTLY and clears NOTHING.
//      ★ THE ONE EXCEPTION IS `binding_failed`, and it is not a "clear" but an **UNDO of this verb's own install**:
//      the pair was adopted one statement earlier and the durable half then failed, so leaving it live would be the
//      half-installed node [[B240]] is about. It is spelled AT the call site, ⛔ not routed through a funnel.
enum class SavedKeyUse : uint8_t {
    installed,       // ★ THE ONE INSTALLING ARM: the record VERIFIED (`adopt_key` re-derived the pub) and the
                     //   `/mrcfg` binding is COMMITTED ⇒ the five-term boot predicate now holds ⇒ BOOT-DURABLE
    zero_team,       // 0 is not a team — ⛔ 0 reads, 0 writes (`put`'s own floor, asked again by the HANDLER)
    // ★★★★ THE STALE-TARGET ARM (QG blocker 1). The offer was BUILT for team A and the `double` arrives some time
    //      later; a `team <id>` over serial/BLE in between moves MEMBERSHIP to B. Installing A's key then would put
    //      A's secret live under a `/mrcfg` that says B — the panel claiming `TEAM KEY ACTIVE` for a binding the
    //      five-term boot restore will REJECT. ⇒ refused, and ⛔ B's live key, B's binding and A's record are all
    //      left exactly as they were. ⓘ It is the K3 HANDLING-TIME re-check discipline applied to an operator's act.
    not_our_team,
    record_unreadable,  // the `/mrcfg` record could not be read ⇒ ★ FAIL CLOSED (C2): an unestablished term is
                        // ⛔ never treated as satisfied, so nothing is adopted and nothing is written
    no_record,       // the keyring holds nothing for that team ⇒ ⛔ nothing to install; the offer should not have been
    store_failed,    // the store is corrupt / would not open ⇒ ⛔ nothing is known, ⛔ nothing is written
    rejected,        // ★ the record does not VERIFY (`adopt_key` refused: pub != derived-from-priv, or degenerate)
    binding_failed,  // ★ the `/mrcfg` ACTIVATION write failed ⇒ THIS VERB'S OWN install is UNDONE (see the block above)
    // ★★★★ THE INVENTORY SENTINEL (2026-08-25, QG blocker 2 — the THIRD instance of this fence, after
    //      `GrantSave::count` and `mrui::InviteGrantState::count`, and it is added for the reason those two exist:
    //      a HAND-WRITTEN inventory had already failed this arc once. The sweep in
    //      `test/test_firmware_team_keyring.cpp` used to RE-TYPE all six values, so an arm appended here would have
    //      been silently unswept while the case went on calling itself exhaustive.
    //      ⇒ THE INVENTORY IS NOW A PROPERTY OF THE ENUM, on two independent axes and neither is a literal:
    //        (1) the case iterates `0 .. count-1`, so an arm added above this line is visited BY CONSTRUCTION;
    //        (2) `saved_key_use_name`'s switch has ⛔ NO `default:`, so an arm added and NOT worded is a
    //            **BUILD FAILURE** under the blanket `-Werror=switch`.
    // ⛔ IT IS ⛔ NOT AN OUTCOME: ⛔ no `use_saved` may return it and ⛔ no answer may carry it. It must stay LAST —
    //    that is what makes it the count.
    count
};
// enum -> string, `default`-LESS for the reason `keyring_verdict_name` gives. ⓘ These tokens are what the panel's
// SECOND row carries (the `UiProvAnswer::reason` mechanism — the SERVICE's own token, ⛔ never a second table);
// ⛔ they name FACTS and carry no material, which is K1's hygiene rule applied to the newest voice.
inline const char* saved_key_use_name(SavedKeyUse v) {
    switch (v) {
        case SavedKeyUse::installed:         return "installed";
        case SavedKeyUse::zero_team:         return "zero_team";
        case SavedKeyUse::not_our_team:      return "not_our_team";
        case SavedKeyUse::record_unreadable: return "record_unreadable";
        case SavedKeyUse::no_record:         return "no_record";
        case SavedKeyUse::store_failed:      return "store_failed";
        case SavedKeyUse::rejected:          return "rejected";
        case SavedKeyUse::binding_failed:    return "binding_failed";
        // ⛔ THE SENTINEL IS ⛔ NOT AN OUTCOME, so it has NO word — spelled out HERE rather than left to a
        //    `default:` for exactly the reason this switch has none (`grant_save_name`'s own note): a `default:`
        //    would swallow a REAL arm added above it, which is the miss the sentinel exists to make impossible.
        case SavedKeyUse::count:             return "?";
    }
    return "?";
}

// ================================================ §UI-16 K6 — SAVED-KEY **RETENTION MANAGEMENT** (spec §4-K6)
// ⛔⛔ IT IS ⛔ NOT KEY ROTATION AND MAY NEVER BE DESCRIBED AS ONE (the ruling's own first sentence): nothing here
//     re-keys a team, re-derives a scalar or replaces material. It REMOVES ONE RETAINED RECORD the operator named
//     and confirmed, so that the fixed FOUR-record bound stops being a dead end once four distinct `team new`s have
//     filled it. K1's P-15 is COMPLETED here, ⛔ not corrected: a full store still performs ZERO writes and ZERO
//     evictions until an operator picks a SPECIFIC inactive record and confirms.
// ★★★★ **TWO EXPLICIT TRANSACTIONS, NEVER ONE DISGUISED ONE** — the ruling's load-bearing clause, and it is a
//      SAFETY property rather than a UX one: `/mrteams` and `/mrcfg` are two separate durable records, so
//      "evict then create" CANNOT be one atomic commit. Hiding both behind one act would let a create that failed
//      its `/mrcfg` write destroy an unrelated saved key on the way. ⇒ `forget` completes and reports ITS OWN
//      verdict; the create/grant is retried BY THE OPERATOR. ⛔ Nothing in this file resumes anything, and `put`
//      above is byte-for-byte unchanged — it still refuses a fifth team loudly and still evicts nothing.
// ⛔⛔ AND THE ACTIVE KEY IS PROTECTED. Removing the record behind the LIVE binding would leave a node reading the
//     team channel today and — the five-term boot restore finding no record — silently unable to tomorrow, which is
//     [[B240]]'s exact shape arriving through a management screen.

// ★★★ THE **ONE** ACTIVE PREDICATE (U1), asked by BOTH the enumeration's marker and `forget`'s refusal, so a row
//     the panel marks `ACTIVE` and a record the service protects can never be two different records. It is the boot
//     restore's term (i) — *"a key is active, and it is for THIS team"* — ⛔ never membership, and ⛔ never "a key is
//     present": the thing that must not be removed is the record the ACTIVE BINDING points at.
// ⓘ `team_id == 0` answers false for the reason it answers false everywhere else here: 0 is never stored, so no
//   record can BE the active one.
inline bool saved_key_is_active(const TeamKeyBinding& b, uint32_t team_id) {
    return team_id != 0 && b.key_active && b.binding_team_id == team_id;
}

// ★★★★ THE METADATA-ONLY ROW. ⛔⛔ THERE IS NO KEY FIELD AND ONE MAY NEVER BE ADDED — that is the whole contract of
//      this type and the reason it exists at all rather than the panel being handed a `TeamKeyRecord`. K1's standing
//      rule is that this file has ⛔ no reader which hands MATERIAL out; an enumeration is exactly where that rule
//      would be lost, so the carrier is SHAPED so it cannot be: two facts, both of which are already public
//      (the team id is public by design — P-2 — and "is this the active one" is a status).
struct SavedKeyEntry {
    uint32_t team_id = 0;
    bool     active  = false;    // ⛔ the marker only; ⛔ never authority to delete (the refusal is `forget`'s)
    // ★ NAMED, ⛔ never implicit tail padding — the `mrnv::TeamKeyRecord::reserved` / `mrui::NearbyRow::reserved`
    //   rule: implicit padding is INDETERMINATE after `SavedKeyEntry{}`, which would make any whole-record compare
    //   (in a test, or in a future de-dup) unsound. ⓘ It costs nothing: the struct measures 8 either way.
    uint8_t  reserved[3] = {};
};
// The whole answer, and it keeps the store's OWN four-valued read VERBATIM (U1 — `UiJoinList`'s rule one feature
// over): "there is no store yet", "the record is corrupt" and "the store would not open" take three different
// operator actions, and collapsing any two is a registered defect class here.
// ⓘ `served` IS A THIRD FACT and not a fifth store state: it says ⛔ NO SEAM ANSWERED AT ALL (a build with no team
//   plane, a partially-wired probe). ⓘ `binding_read` is a FOURTH: the ACTIVE marker's authority is the `/mrcfg`
//   binding, and when THAT could not be read the marker is UNESTABLISHED — so the list FAILS CLOSED and offers ⛔ no
//   row at all, rather than offering rows one of which might silently be the protected one (C2).
struct SavedKeyList {
    SavedKeyEntry     rec[mrnv::kTeamKeyRecs] = {};
    uint8_t           n            = 0;
    mrnv::TeamKeyRead st           = mrnv::TeamKeyRead::absent;
    bool              served       = false;
    bool              binding_read = false;
};

// ---- the REMOVAL's outcome ---------------------------------------------------------------------------------------
// ★★★ A FOURTH OUTCOME TYPE, ⛔ NOT `KeyringVerdict` REUSED, and the argument is `SavedKeyUse`'s own one screen up:
//     that vocabulary is `put`'s (`unchanged` = the flash-wear guard, `refused` = a policy refusal on a WRITE of new
//     material), and folding a REMOVAL into it would make `unchanged` mean two unrelated things — "your material was
//     already stored" and "there was nothing to remove". These are different questions with different remedies.
// ⛔⛔ `nv_save_failed` IS **A FAILED SAVE**, ⛔ NEVER "NOTHING CHANGED" — the ruling says so in as many words, and
//     [[B193]] is why: a backend can fail AFTER a partial write, so the honest statement is that the removal did not
//     COMPLETE. The real backend's power-cut behaviour is METAL-ONLY (M2) and no voice above may claim otherwise.
enum class KeyringForget : uint8_t {
    forgotten,           // ★ the record is gone, the survivors are compacted, the vacated tail is WIPED, ONE save
    zero_team,           // 0 is not a team ⇒ ⛔ 0 reads, 0 writes (`put`'s floor, asked again by this verb)
    // ★★★ FAIL CLOSED (C2): the ACTIVE marker's authority could not be read, so "is this the protected record" is
    //     UNESTABLISHED — and an unestablished term is ⛔ never treated as satisfied. ⛔ 0 keyring reads, 0 writes.
    binding_unreadable,
    // ★★★★ THE PROTECTION, AND IT IS THE SLICE'S HEADLINE REFUSAL: the target IS the record the ACTIVE BINDING
    //      points at. ⛔ ZERO writes, ⛔ nothing removed, ⛔ nothing compacted — the live key, the binding, the
    //      membership and ALL FOUR records are exactly as they were found.
    active_key,
    no_record,           // an ABSENT store, or a store with no record for that team ⇒ ⛔ 0 writes
    store_failed,        // invalid / io_failed ⇒ ⛔ nothing is known, ⛔ 0 writes (⛔ never collapsed into `no_record`)
    nv_save_failed,      // the ONE save attempt failed ⇒ the removal did ⛔ NOT complete (see the block above)
    // ★★★★ THE INVENTORY SENTINEL — the FOURTH instance of this fence (`GrantSave::count`,
    //      `mrui::InviteGrantState::count`, `SavedKeyUse::count`), added for the reason those three exist: a
    //      HAND-WRITTEN inventory has already failed this arc once. Two independent axes, neither a literal:
    //        (1) the sweep iterates `0 .. count-1`, so an arm added above this line is visited BY CONSTRUCTION;
    //        (2) `keyring_forget_name`'s switch has ⛔ NO `default:`, so an arm added and NOT worded is a
    //            **BUILD FAILURE** under the blanket `-Werror=switch`.
    // ⛔ IT IS ⛔ NOT AN OUTCOME: ⛔ no `forget` may return it. It must stay LAST — that is what makes it the count.
    count
};
// enum -> string, `default`-LESS for the reason `keyring_verdict_name` gives. ⓘ These tokens are what the panel's
// SECOND row carries and what the console prints; ⛔ they name FACTS and carry ⛔ no material.
inline const char* keyring_forget_name(KeyringForget v) {
    switch (v) {
        case KeyringForget::forgotten:          return "forgotten";
        case KeyringForget::zero_team:          return "zero_team";
        case KeyringForget::binding_unreadable: return "binding_unreadable";
        case KeyringForget::active_key:         return "active_key";
        case KeyringForget::no_record:          return "no_record";
        case KeyringForget::store_failed:       return "store_failed";
        case KeyringForget::nv_save_failed:     return "nv_save_failed";
        // ⛔ THE SENTINEL IS ⛔ NOT AN OUTCOME, so it has NO word — spelled out HERE rather than left to a
        //    `default:`, which would swallow a REAL arm added above it (`grant_save_name`'s own note).
        case KeyringForget::count:              return "?";
    }
    return "?";
}

// ★★★★ THE COMMIT'S **SECOND AUTHORITY** (QG blocker 1), AND IT IS PURE SO IT CAN BE ATTACKED — added 2026-08-25.
//      `ITeamKeyBinding::commit_active` writes the ACTIVE BINDING into the `/mrcfg` record it has just loaded. That
//      record carries the MEMBERSHIP, and a binding for a team the record does not name is a binding that LIES: the
//      five-term boot restore compares the two (term (ii)) and comes up KEYLESS, so the operator is told a key is
//      active that no reboot will ever install. ⇒ EVERY writer asks this FIRST and refuses without writing.
// ⓘ WHY A FREE PREDICATE AND NOT A LINE INSIDE THE DEVICE WRITER: `src/firmware_config.cpp` is compiled by NEITHER
//   the native suite NOR the simulator (§B115), so a guard written only there is a guard no gate can drive and no
//   mutation can redden. Declared here, the REAL writer, the two test fakes and the probe's binding all call the
//   SAME one authority (U1) — and `--target=teamkeyring` can attack it at match count 1.
// ⛔ IT IS ⛔ NOT A SUBSTITUTE FOR `use_saved`'s OWN RE-CHECK: that one refuses BEFORE a secret is adopted and can
//    say WHY; this one is the last fence in front of the write, in the shape K3's re-check (4) already uses.
inline bool commit_membership_ok(uint32_t record_team_id, uint32_t want_team_id) {
    return want_team_id != 0 && record_team_id == want_team_id;
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

// ⓘ §UI-16 K5 — FORWARD-DECLARED ONLY. `ITeamKeyBinding` is DEFINED in the K3 section at the foot of this file (its
//   own note says why it lives there: `blob_put_team_channel_key`, the one `/mrcfg` conversion authority, is in a
//   file that includes THIS one, so the seam names the operations and leaves the assignments where they already
//   live). `TeamKeyringService::use_saved` needs both seams, so it is DECLARED in the class below and DEFINED after
//   that struct — ⛔ never by hoisting the struct, which would move code a landed battery anchors by text (C1).
struct ITeamKeyBinding;

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

    // ================================================================ §UI-16 K5 — THE **PRESENCE** QUESTION (P-2b)
    // ★★★★ IT ANSWERS A **BOOLEAN**, AND THAT IS THE WHOLE OF ITS CONTRACT: *"is a key for this team RETAINED?"* —
    //      ⛔ never *"give me the key"*. K1's standing rule (see the header) is that this file has no reader which
    //      hands MATERIAL out, and this does not become one: the only thing that leaves is one bit, and the only
    //      caller is the join adapter deciding whether to OFFER a screen.
    // ⛔⛔ ANSWERING TRUE INSTALLS NOTHING AND IMPLIES NOTHING (P-2b). A retained record plus knowledge of the public
    //     team id is exactly the pair the ruling says may ⛔ never reactivate a secret; what it earns is an OFFER
    //     with `BACK` selected, which is why K5 is a SCREEN and not a rule.
    // ★ ZERO WRITES, and it FAILS CLOSED (C2): an ABSENT, CORRUPT or UNREADABLE store answers **false** — ⛔ never
    //   "probably". An offer made against a store nobody could read would end on a refusal the operator did not need
    //   to be walked into. ⓘ The three are one answer HERE and stay distinguishable where it matters — `use_saved`
    //   below tells `no_record` and `store_failed` apart, because there the operator has ASKED.
    bool has_record(uint32_t team_id) {
        if (team_id == 0) return false;                    // ⛔ 0 is never stored ⇒ ⛔ 0 loads, and never an offer
        mrnv::TeamKeyBlob cur{};
        SecretWipeGuard<mrnv::TeamKeyBlob> cguard{cur};    // ⚠ SECRET-BEARING: the same one guard, the same rule
        if (_store.load(cur) != mrnv::TeamKeyRead::ok) return false;
        team_key_clamp_count(cur);
        return team_key_find(cur, team_id) >= 0;
    }

    // ================================================================ §UI-16 K5 — THE **EXPLICIT** ACTIVATION
    // ⛔ DECLARED HERE, DEFINED BELOW `ITeamKeyBinding` (see the forward declaration above this class). The contract
    //    is written at the definition, with the order it enforces.
    SavedKeyUse use_saved(uint32_t team_id, ITeamKeyLive& live, ITeamKeyBinding& binding);

    // ================================================================ §UI-16 K6 — THE **METADATA-ONLY** ENUMERATION
    // ⛔ DECLARED HERE, DEFINED BELOW `ITeamKeyBinding`, exactly as `use_saved` is and for the same reason.
    // ★★★ ZERO WRITES ON EVERY PATH, and ⛔ NO KEY BYTE LEAVES: the loaded blob is scope-guarded and WIPED, and what
    //     is copied out is `{team_id, active}` per record and nothing else. That is K1's *"no reader hands MATERIAL
    //     out"* rule surviving the one verb that most invites breaking it.
    SavedKeyList list(ITeamKeyBinding& binding);

    // ================================================================ §UI-16 K6 — THE **CONFIRMED** REMOVAL
    // ⛔ DECLARED HERE, DEFINED BELOW `ITeamKeyBinding`. The ORDER it enforces is written at the definition.
    KeyringForget forget(uint32_t team_id, ITeamKeyBinding& binding);

  private:
    // ★★★ THE GOVERNANCE, SPELLED ONCE (U1) — QG blocker 1's fix, and the shape matters: every non-installing return
    //     of `restore` goes through this, so a refusal arm added later CANNOT forget to leave the node keyless. A
    //     hand-written `live.clear_key();` before each of the six returns is exactly the shape that gets a seventh
    //     added and missed (the reason `apply_team` uses a scope guard for its wipe).
    // ⛔ It is NOT "clear if a key is present": the seam is idempotent and the service must not hold a belief about
    //    live state it did not establish.
    // ⛔⛔ IT IS THE **BOOT RESTORE'S** GOVERNANCE AND ⛔ NOT A GENERAL ONE — ★ STATED HERE 2026-08-25 (QG blocker
    //    1), because §UI-16 K5 briefly made it a template and that was WRONG: the clearing is correct exactly where
    //    THE LIVE KEY IS THE SUSPECT (a previous boot step may have installed a key this node must not hold). In
    //    `use_saved` the live key belongs to the team `/mrcfg` currently NAMES and is INNOCENT, so that verb refuses
    //    surgically and clears nothing (see its own block). ⇒ this stays `KeyringRestore`'s, with ONE caller family.
    static KeyringRestore refuse(ITeamKeyLive& live, KeyringRestore why) {
        live.clear_key();
        return why;
    }

    ITeamKeyStore& _store;
};

// ================================================================ §UI-16 K3 — THE GRANT-RECEIVE PERSISTENCE
// ★★★ THE ORDERING RULING (✅ F-10, spec §4-K3). `mr_ui_on_push(pu)` is the FIRST statement in `fw_main`'s drain
//     loop, so the draft's *"gate the note on the save"* was not expressible at all. ⇒ for `team_key_received` the
//     PERSISTENCE RUNS FIRST and ⛔ **only a `saved` verdict forwards the push to the UI**. The panel therefore
//     cannot say `TEAM KEY RECEIVED` for a key that is RAM-only: not because a reviewer checked a gate, but because
//     the push never reaches the renderer.
// ★★ THE FOUR HANDLING-TIME RE-CHECKS, AND THEY CLOSE A REAL RACE RATHER THAN RESTATING THE RECEIVER'S. A push is
//    ENQUEUED inside `Node::team_key_grant_receive` and DRAINED some time later; membership can move in between
//    (`team 0`, a switch, a `/mrcfg` write from the companion). The core's own checks answered questions about the
//    node AT RX TIME. These four ask them again AT HANDLING TIME, and each names a different authority:
//      (1) `pu.team_id != 0`                        — ⛔ id 0 is never stored (the keyring refuses it too, and this
//                                                     refuses it EARLIER so the load never happens);
//      (2) `pu.team_id == live NodeConfig.team_id`  — the LIVE membership, which `team 0` clears immediately;
//      (3) the LIVE key is present                  — `set_team_id` wipes the pair on a switch, so "adopted at RX"
//                                                     does not mean "still held now"; ⛔ never persist a wiped pair;
//      (4) `pu.team_id == the PERSISTED record's `team_id`` — the /mrcfg record, which is a DIFFERENT authority from
//                                                     (2) and legitimately disagrees with it between a live change
//                                                     and its save. ⛔ Marking a key ACTIVE against a record that
//                                                     names another team is exactly QG blocker 3 arriving by push.
//    ⛔ ANY ONE FAILING ⇒ ZERO WRITES on BOTH records and the UI is ⛔ not told a key was adopted.
// ⛔⛔ SECRET HYGIENE IS K1's, UNCHANGED AND ABSOLUTE: no key byte is printed, echoed, logged or placed in an outcome
//    — the verdicts below name FACTS ("the record names another team", "the one save attempt failed"), ⛔ never
//    material. ★ AND THIS SERVICE HOLDS NO SECRET AT ALL: it takes the live pair as two `const uint8_t*` and hands
//    them straight to `put()` / `commit_active()`, so there is no transient copy here to wipe (`TeamKeyringService`
//    owns the one that exists, under its own scope guard).

// ---- the /mrcfg half's seam ------------------------------------------------------------------------------------
// ★★★ WHY A THIRD SEAM AND ⛔ NOT `ICfgStore` DIRECTLY (U1 was checked first, twice): the `/mrcfg` candidate for an
//     ACTIVATION is composed by `mrfw::blob_put_team_channel_key` — the ONE conversion authority for key material
//     into `mrnv::Blob` (U2) — which lives in `src/firmware_provisioning_service.h`, and THAT file includes THIS
//     one. Reaching for it here would invert the include graph. ⇒ this seam names the two OPERATIONS the decision
//     needs and leaves the four assignments where their authority already lives; the device binding
//     (`src/firmware_config.cpp`) is a forward with ⛔ no decision in it, exactly as `DeviceTeamKeyLive` is.
// ⓘ `read` REUSES `TeamKeyBinding` above rather than declaring a parallel struct: the boot restore and the grant
//   receive ask the SAME record the SAME five questions, and two structs would be two things free to drift.
struct ITeamKeyBinding {
    virtual ~ITeamKeyBinding() = default;
    // The PERSISTED `/mrcfg` facts. `false` = the record could not be read ⇒ ★ the receive FAILS CLOSED (C2): an
    // unestablished term is ⛔ never treated as satisfied, so nothing is written and nothing is claimed.
    // ⚠ `out.committed_pub` may point into storage the callee owns; it must stay valid for the duration of the call
    //   and is ⛔ never retained past it (the same contract `TeamKeyBinding` carries for the boot restore).
    virtual bool read(TeamKeyBinding& out) = 0;
    // Persist {the committed witness, the ACTIVE binding} for `team_id`. `false` = THE WRITE FAILED. ⛔ Never
    // "nothing was written" — a failed save may have written PARTIALLY ([[B193]]), and no voice above may say otherwise.
    virtual bool commit_active(uint32_t team_id, const uint8_t pub[32], const uint8_t priv[32]) = 0;
};

// ================================================================ §UI-16 K5 — `USE SAVED KEY`, THE ONE ACTIVATION
// ★★★★ THE ORDER IS THE CONTRACT, AND IT IS **K3's ORDER WITH ITS FIRST STEP ALREADY DONE**: K3 writes *the key
//      durably FIRST, then the activation*, because a reboot landing between the two must find a retained record
//      with no active binding (which comes up KEYLESS — the honest answer) rather than a binding with no key behind
//      it (a binding that lies). ★ HERE THE KEY IS **ALREADY DURABLE** — it is the retained record — so the only
//      write left is the ACTIVATION, and the step that precedes it is the VERIFICATION.
//        refuse id 0 -> ★ **RE-CHECK THE PERSISTED MEMBERSHIP** -> load -> absent/unreadable, told apart -> find the
//        team's record -> **ADOPT IT LIVE** (which is what verifies it) -> **COMMIT THE `/mrcfg` ACTIVE BINDING**
//        -> installed.
// ★★★ EVERY LINE OF IT IS AN EXISTING AUTHORITY **CALLED** (U1/U2), and that is a REQUIREMENT of this slice rather
//     than a description of it — ⛔ there is ⛔ NO second install sequence anywhere in this tree:
//       · the LIVE install is `ITeamKeyLive::adopt_key` — the boot restore's own term (v), i.e.
//         `Node::team_channel_key_adopt`, which RE-DERIVES the public half from the private one and REFUSES a record
//         that does not verify. ⛔ Not `team_channel_key_load` and ⛔ not a derivation re-spelled here;
//       · the DURABLE half is `ITeamKeyBinding::commit_active` — K3's own writer, i.e. `blob_put_team_channel_key`
//         (the ONE conversion of key material into `mrnv::Blob`) plus the two binding assignments;
//       · the REFUSAL is ⛔ **NOT** `refuse()` — see `SavedKeyUse`'s block: this verb's refusals are SURGICAL and
//         clear nothing, and the one arm that wipes (`binding_failed`) is UNDOING ITS OWN INSTALL, at the call site.
// ★★★★ WHAT `installed` BUYS, STATED AS THE FIVE TERMS BECAUSE THAT IS HOW IT IS PINNED: after this returns
//      `installed`, `/mrcfg` names this team as MEMBERSHIP (the JOIN wrote that, before the offer was ever shown) and
//      as the ACTIVE BINDING, and it WITNESSES the very public half the keyring holds — so `restore()`'s (i)…(v) all
//      hold and the key survives the power cycle. ⛔ THE CLAIM IS NOT MADE BY THIS COMMENT: the suite DRIVES
//      `restore()` against the state this function wrote.
// ⛔⛔ SECRET HYGIENE IS K1's, UNCHANGED: the material is loaded into the ONE scope-guarded transient, handed to the
//     two seams, and wiped on EVERY exit. ⛔ No key byte reaches a verdict, a token, a log or the panel — the
//     outcomes name FACTS (`rejected`, `binding_failed`), and `saved_key_use_name` is what the screen may print.
// ⚠ THE LIMIT OF THE CLAIM, in this file's standing words: `commit_active` returning false may still have written
//   PARTIALLY ([[B193]]) — so `binding_failed` means *"the activation did not complete"*, ⛔ never *"no flash was
//   changed"*. The node is left KEYLESS, which is true whatever the flash did.
inline SavedKeyUse TeamKeyringService::use_saved(uint32_t team_id, ITeamKeyLive& live, ITeamKeyBinding& binding) {
    // ⛔ THE HANDLER's OWN FLOOR (C2), asked here as well as in `put` and for `receive`'s stated reason: neither verb
    //    may rely on the other having run. ⓘ ZERO reads, ZERO writes, ⛔ nothing cleared.
    if (team_id == 0) return SavedKeyUse::zero_team;

    // ★★★★ (1) THE MEMBERSHIP RE-CHECK, AT **HANDLING** TIME — QG blocker 1, and it is K3's re-check (2)/(4)
    //      discipline applied to an OPERATOR'S ACT rather than to a drained push. The reason is identical and it is
    //      not hypothetical: the offer screen was BUILT from a join that had just returned, and the `double` arrives
    //      some seconds later. A `team <id>` over serial or BLE in between moves the membership underneath it, and
    //      the id this verb was handed is then STALE — it names the team the operator was looking at, ⛔ not the
    //      team this node is in.
    // ★★★ IT IS ASKED OF THE **PERSISTED** RECORD, which is the same authority `commit_active` is about to write
    //     into — so the term this refuses on is the very term the boot restore will compare (term (ii)). Asking the
    //     UI, or trusting the id it retained, would be asking the party that is already wrong.
    // ⛔ AND IT IS ASKED **BEFORE THE KEYRING IS EVEN OPENED**: a stale target costs ZERO secret-bearing loads,
    //    ZERO adopts and ZERO writes. ⓘ FAIL CLOSED on an unreadable record (C2) — an unestablished term is never
    //    treated as satisfied.
    TeamKeyBinding cur_bind{};
    if (!binding.read(cur_bind))                       return SavedKeyUse::record_unreadable;
    if (cur_bind.membership_team_id != team_id)        return SavedKeyUse::not_our_team;

    mrnv::TeamKeyBlob cur{};
    SecretWipeGuard<mrnv::TeamKeyBlob> cguard{cur};        // ⚠ SECRET-BEARING FROM THE NEXT LINE ON
    const mrnv::TeamKeyRead st = _store.load(cur);
    // ⛔ THE TWO ABSENT-ISH ANSWERS ARE **NOT** COLLAPSED, for `keyring_err_of_unreadable`'s reason one screen up:
    //    "there is no record for this team" and "the store could not be read at all" take different operator
    //    actions, and the operator has explicitly ASKED here, so he gets the one that is true.
    // ⛔⛔ AND NEITHER ARM CLEARS ANYTHING (the SURGICAL refusal, see `SavedKeyUse`'s block): past the re-check the
    //     node's membership IS this team, so any live key it holds is this team's — installed by a serial import a
    //     moment ago, say — and a keyring that could not be read is ⛔ no reason to destroy it.
    if (st == mrnv::TeamKeyRead::absent) return SavedKeyUse::no_record;
    if (st != mrnv::TeamKeyRead::ok)     return SavedKeyUse::store_failed;
    team_key_clamp_count(cur);
    const int idx = team_key_find(cur, team_id);
    if (idx < 0) return SavedKeyUse::no_record;

    // ★★ THE VERIFICATION **IS** THE INSTALL (the boot restore's term (v), the same call): a record whose pub does
    //    not derive from its priv installs NOTHING. ⛔ There is deliberately no fallback that loads the pair
    //    verbatim — a pair whose halves disagree seals posts nobody can read. ⓘ `adopt_key` leaves the live state
    //    UNTOUCHED on a refusal (its own contract), so there is nothing to undo and ⛔ nothing is cleared.
    if (!live.adopt_key(cur.rec[idx].team_ch_pub, cur.rec[idx].team_ch_priv))
        return SavedKeyUse::rejected;
    // ★★★ THEN THE DURABLE ACTIVATION, AND A FAILURE HERE **UNDOES THIS VERB'S OWN INSTALL** (C2): the operator
    //     asked for a key that would still be there after a reboot, and half of that is worse than none — a live key
    //     with no binding is a node that reads the channel today and silently cannot tomorrow, which is [[B240]]'s
    //     exact shape. ⓘ THE UNDO IS SAFE PRECISELY BECAUSE OF THE RE-CHECK ABOVE: membership is this team, so the
    //     pair being wiped is the one adopted two lines up and ⛔ never another team's. ★ It is spelled HERE, at the
    //     one arm that owns it, ⛔ not routed through the boot restore's clearing funnel.
    // ⚠ THE LIMIT OF THE CLAIM, in this file's standing words: a `commit_active` that returns false may still have
    //   written PARTIALLY ([[B193]]) — so `binding_failed` means *"the activation did not complete"*, ⛔ never
    //   *"no flash was changed"*. THE RETAINED RECORD IS UNTOUCHED on this path, and that IS established.
    if (!binding.commit_active(team_id, cur.rec[idx].team_ch_pub, cur.rec[idx].team_ch_priv)) {
        live.clear_key();
        return SavedKeyUse::binding_failed;
    }
    return SavedKeyUse::installed;
}

// ================================================================ §UI-16 K6 — THE ENUMERATION, DEFINED
// ★★★★ THE ORDER IS THE CONTRACT, AND ITS FIRST STEP IS THE ONE A READER WOULD NOT EXPECT: **THE BINDING IS READ
//      FIRST, BEFORE THE KEYRING IS OPENED AT ALL.** The ACTIVE marker's authority is `/mrcfg`, and a list whose
//      marker is unestablished is a list in which the operator cannot tell the PROTECTED record from the three he
//      may remove. ⇒ an unreadable binding yields ⛔ ZERO ROWS (C2), ⛔ not four unmarked ones.
// ⛔ EVERY ARM IS READ-ONLY: ⛔ zero writes, ⛔ nothing seeded, ⛔ nothing compacted, ⛔ nothing installed or cleared.
//    An ABSENT store is NOT seeded here — `put` owns that, and seeding on a READ would spend a flash write to answer
//    a question (and would create a record the operator never asked for).
// ⛔⛔ AND ⛔ NO KEY BYTE LEAVES THIS FUNCTION. The blob is the same scope-guarded transient every other verb uses;
//     what is copied out is the PUBLIC team id and one status bit per record.
inline SavedKeyList TeamKeyringService::list(ITeamKeyBinding& binding) {
    SavedKeyList out{};
    out.served = true;                    // ⛔ *"a seam answered"*, ⛔ never *"the answer was good"* (UiJoinList's rule)

    TeamKeyBinding bind{};
    if (!binding.read(bind)) return out;   // `binding_read` stays FALSE ⇒ ⛔ zero rows, ⛔ zero keyring loads
    out.binding_read = true;

    mrnv::TeamKeyBlob cur{};
    SecretWipeGuard<mrnv::TeamKeyBlob> cguard{cur};    // ⚠ SECRET-BEARING FROM THE NEXT LINE ON — the one guard
    out.st = _store.load(cur);
    // ⛔ THE THREE NON-`ok` ANSWERS ARE CARRIED VERBATIM AND ⛔ NOT COLLAPSED (`keyring_err_of_unreadable`'s reason):
    //    a fresh device, a corrupt record and a store that would not open take three different operator actions.
    if (out.st != mrnv::TeamKeyRead::ok) return out;
    team_key_clamp_count(cur);
    for (uint16_t i = 0; i < cur.count; ++i) {
        // ⛔ 0 IS NEVER STORED, so a record carrying it is corruption and is ⛔ not offered as a removable row: it
        //    could not be removed anyway (`forget` refuses id 0 before it reads anything).
        if (cur.rec[i].team_id == 0) continue;
        out.rec[out.n].team_id = cur.rec[i].team_id;
        out.rec[out.n].active  = saved_key_is_active(bind, cur.rec[i].team_id);   // ★ THE ONE PREDICATE (U1)
        ++out.n;
    }
    return out;
}

// ================================================================ §UI-16 K6 — THE REMOVAL, DEFINED
// ★★★★ THE ORDER IS THE CONTRACT: refuse id 0 -> READ THE BINDING (fail closed) -> **REFUSE THE ACTIVE RECORD** ->
//      load -> absent/unreadable, told apart -> find the record BY ITS FULL 32-BIT ID -> COMPACT -> **WIPE THE
//      VACATED TAIL** -> EXACTLY ONE save.
//      ⇒ a zero id costs ⛔ ZERO reads and ZERO writes; an unreadable binding costs ZERO keyring reads and ZERO
//        writes; the ACTIVE record costs ZERO writes; an absent, corrupt or unreachable store costs ZERO writes; a
//        record that is not there costs ZERO writes. ★ EXACTLY ONE PATH WRITES, AND IT WRITES EXACTLY ONCE.
// ★★★ THE PROTECTION IS ASKED **BEFORE THE KEYRING IS OPENED**, deliberately: the active record must be refused
//     whatever state the store is in, and refusing it after a load would make the refusal depend on the store being
//     readable — i.e. a corrupt store could turn a PROTECTION into a different answer.
// ⛔⛔ THE IDENTITY IS THE **FULL 32-BIT `team_id`** (`team_key_find`, the one lookup): ⛔ never the six-hex display
//     fingerprint (24 of 32 bits — [[B48]]'s class, and pin 7 drives two teams that share those digits), ⛔ never a
//     row index (the list skips a corrupt 0-id record, so an index is not an identity — §B66), and ⛔ never a name
//     (there is no team label in this firmware at all — K1's `⛔ NO LABELS` clause).
// ★★★★ THE COMPACTION IS **ORDER-PRESERVING AND DETERMINISTIC**, ⛔ not a swap-with-the-last: every surviving record
//      keeps its bytes AND its relative order, so `list()` does not re-order under an operator who is about to
//      remove a second one. ⓘ `reserved[4]` being a NAMED member is what makes "byte-identical" checkable at all.
// ⛔⛔ AND THE VACATED SLOT IS `crypto_wipe`d, WHICH IS THE SECURITY HALF OF THE COMPACTION: the shift leaves the
//     TAIL holding a byte-for-byte DUPLICATE of a record that is still live, so a compaction that merely decremented
//     the count would persist a second copy of a team's PRIVATE key in a slot nothing reads and nothing clears —
//     recoverable from a flash dump long after the operator believed a key had been removed. ⛔ `crypto_wipe`,
//     ⛔ never `memset` (`Node::team_channel_key_clear`'s own reason: a compiler may elide the latter).
// ⚠ THE LIMIT OF THE CLAIM, in this file's standing words: a `save` that returns false may have written PARTIALLY
//   ([[B193]]) — so `nv_save_failed` means *"the removal did not complete"*, ⛔ never *"no flash was changed"*. The
//   keyring's power-cut behaviour is METAL-ONLY (M2) and is owed as a bench part.
inline KeyringForget TeamKeyringService::forget(uint32_t team_id, ITeamKeyBinding& binding) {
    if (team_id == 0) return KeyringForget::zero_team;                  // ⛔ 0 reads, 0 writes

    TeamKeyBinding bind{};
    if (!binding.read(bind))                     return KeyringForget::binding_unreadable;   // ⛔ 0 keyring reads
    if (saved_key_is_active(bind, team_id))      return KeyringForget::active_key;           // ★ PROTECTED, 0 writes

    mrnv::TeamKeyBlob cur{};
    SecretWipeGuard<mrnv::TeamKeyBlob> cguard{cur};    // ⚠ SECRET-BEARING FROM THE NEXT LINE ON
    const mrnv::TeamKeyRead st = _store.load(cur);
    if (st == mrnv::TeamKeyRead::absent) return KeyringForget::no_record;
    if (st != mrnv::TeamKeyRead::ok)     return KeyringForget::store_failed;
    team_key_clamp_count(cur);
    const int idx = team_key_find(cur, team_id);
    if (idx < 0) return KeyringForget::no_record;                       // ⛔ 0 writes — there is nothing to remove

    // ★ THE COMPACTION. Every later record moves down ONE slot, in order, WHOLE (U2 — ⛔ never field by field).
    for (uint16_t i = static_cast<uint16_t>(idx); i + 1 < cur.count; ++i) cur.rec[i] = cur.rec[i + 1];
    cur.count = static_cast<uint16_t>(cur.count - 1);
    // ★★ THE VACATED TAIL, WIPED — see the block above for why this line is not tidiness.
    crypto_wipe(&cur.rec[cur.count], sizeof cur.rec[cur.count]);

    if (!_store.save(cur)) return KeyringForget::nv_save_failed;        // ⛔ never reported as "nothing changed"
    return KeyringForget::forgotten;
}

// ---- what the handler is given, at HANDLING time -----------------------------------------------------------------
// ⓘ Every field is a RE-CHECK TERM or the material itself. ⛔ THERE IS NO `name` FIELD and its absence is the ruling,
//   not an omission: the granter's optional `name=` rides the push and stops there (`lib/core/node.cpp:264-266`), and
//   there is no team-label store anywhere in this codebase for it to reach (K1's own `⛔ NO LABELS` clause).
// ★ `live_pub`/`live_priv` come from `Node::team_channel_pub()` / `team_channel_priv()`, which return `nullptr`
//   while the node is KEYLESS — so their nullness IS re-check (3)'s answer, taken from the ONE authority rather than
//   carried as a second boolean that could disagree with it (U1).
struct TeamKeyGrant {
    uint32_t       push_team_id = 0;        // (1)/(2)/(4) — the team the GRANT named (`Push::team_id`)
    uint32_t       live_team_id = 0;        // (2) — `g_node.config().team_id`, the LIVE membership
    const uint8_t* live_pub     = nullptr;  // (3) + the material — null while keyless
    const uint8_t* live_priv    = nullptr;  // (3) + the material — null while keyless
};

// ---- the verdict -------------------------------------------------------------------------------------------------
// ★★ `saved` IS THE ONLY VALUE THAT MAY REACH THE PANEL AS `TEAM KEY RECEIVED` (spec §8 S-25, F-10). Every other
//    arm means the key is LIVE IN RAM AND NOT DURABLE — which is a true and different thing to say (S-26/S-27).
enum class GrantSave : uint8_t {
    saved,              // ★ both records now hold this team's key, or already did (zero writes) — the UI may be told
    zero_team,          // re-check (1) — ⛔ 0 loads, 0 writes
    not_our_team,       // re-check (2) — LIVE membership moved between RX and drain
    no_live_key,        // re-check (3) — the pair was wiped between RX and drain; ⛔ never persist a wiped key
    record_mismatch,    // re-check (4) — the PERSISTED record names another team
    record_unreadable,  // the `/mrcfg` record could not be read ⇒ FAIL CLOSED, ⛔ zero writes
    keyring_failed,     // the `/mrteams` write refused or failed — `err` names WHICH (⛔ never material)
    binding_failed,     // the `/mrcfg` activation write failed AFTER the key landed durably
    // ★★★★ THE INVENTORY SENTINEL (2026-08-25, the §UI-16 N6b precedent applied again — see
    //      `mrui::InviteGrantState::count`, which exists because a HAND-WRITTEN inventory had already failed this
    //      arc: an array stayed short while its hand-typed literal stayed right, and the sweep went on calling
    //      itself exhaustive). The totality case used to walk `0 .. binding_failed`, i.e. it re-typed the LAST
    //      ENUMERATOR — so an outcome appended after `binding_failed` would have been silently unswept.
    //      ⇒ THE INVENTORY IS NOW A PROPERTY OF THE ENUM, on two independent axes and neither is a literal:
    //        (1) the case iterates `0 .. count-1`, so an outcome added above this line is visited BY CONSTRUCTION;
    //        (2) `grant_save_name`'s switch has ⛔ NO `default:`, so an outcome added and NOT worded is a
    //            **BUILD FAILURE** under the blanket `-Werror=switch`.
    //      ⇒ ★ AN OUTCOME ADDED WITHOUT A WORD DOES NOT COMPILE; ONE ADDED WITH A WORD IS SWEPT AUTOMATICALLY.
    //        There is no pair left to keep in sync.
    // ⛔ IT IS ⛔ NOT AN OUTCOME and no `GrantSaveResult` may ever carry it: it names how many there are, nothing
    //    more. ⛔ It must stay LAST — that is what makes it the count.
    count
};
// enum -> string, `default`-LESS for the reason `keyring_verdict_name` gives: this project has shipped THREE
// enum->string defects the byte-identity gate was structurally blind to.
inline const char* grant_save_name(GrantSave g) {
    switch (g) {
        case GrantSave::saved:             return "saved";
        case GrantSave::zero_team:         return "zero_team";
        case GrantSave::not_our_team:      return "not_our_team";
        case GrantSave::no_live_key:       return "no_live_key";
        case GrantSave::record_mismatch:   return "record_mismatch";
        case GrantSave::record_unreadable: return "record_unreadable";
        case GrantSave::keyring_failed:    return "keyring_failed";
        case GrantSave::binding_failed:    return "binding_failed";
        // ⛔ THE SENTINEL IS ⛔ NOT AN OUTCOME, so it has NO name — and it is spelled out HERE rather than left to a
        //    `default:` for exactly the reason this switch has none: a `default:` would swallow a REAL outcome added
        //    above it, which is precisely the miss the sentinel was introduced to make impossible. `"?"` is the same
        //    refusal the out-of-range return below answers with — ⛔ never a plausible-looking word.
        case GrantSave::count:             return "?";
    }
    return "?";
}

// ================================================================ [[B243]] — THE UI ROUTING VERDICT (QG, 2026-08-25)
// ★★★★ **A BOOLEAN WAS THE WRONG SHAPE AND IT PRODUCED A FALSE PANEL STATEMENT.** ⛔ CORRECTED IN PLACE, and the
//      withdrawn shape is kept visible because it is the lesson: `team_key_grant_persist` used to answer
//      `outcome == saved`, so `fw_main`'s `else` sent EVERY refusal to the failed-save door and the panel said
//      `TEAM KEY ACTIVE` / `NOT SAVED` / `LOST ON REBOOT` about receipts for which **nothing is active at all** —
//      `no_live_key` (the pair was WIPED between RX and drain), `not_our_team` (we are no longer in that team),
//      `zero_team` (the receipt named no team). A panel that invents an active key is the same defect class as a
//      panel that claims a durable one; ⛔ the direction of the lie does not excuse it.
// ★★★ SO THE VERDICT IS THREE-VALUED, AND THE THIRD VALUE IS **SILENCE** — the honest answer when there is no true
//     team-key sentence to say (C2: refuse rather than invent). ⓘ The CONSOLE half is untouched on every arm: the
//     operator watching the serial line still sees the receipt whatever this answers. Silence is the PANEL's.
enum class GrantUiRoute : uint8_t {
    received,        // ★ the push is FORWARDED unchanged -> `mr_ui_on_push` -> `TEAM KEY RECEIVED` (S-25)
    active_unsaved,  // ★ the second door -> `TEAM KEY ACTIVE` / `NOT SAVED` / `LOST ON REBOOT` (S-26/S-27)
    suppressed,      // ⛔ NEITHER door — the panel says nothing, because nothing true can be said
    // The inventory sentinel, for the reason `GrantSave::count` carries one and enforced the same way. ⛔ Not a route.
    count
};

// ★★★★ THE CLASSIFICATION, AND IT IS **STRUCTURAL RATHER THAN A TASTE JUDGEMENT**: the dividing line is exactly
//      *where the arm sits relative to re-check (3)* in `TeamKeyGrantService::receive` (`:545` area).
//      ⇒ EVERY arm reached AFTER re-check (3) has, by control flow and not by assumption, established all three of:
//          · `push_team_id != 0`                (re-check 1 passed)
//          · `push_team_id == live_team_id`     (re-check 2 passed — this IS our current team)
//          · `live_pub && live_priv`            (re-check 3 passed — the pair IS live in RAM)
//        i.e. **the team key genuinely IS ACTIVE**, and the only thing that failed is DURABILITY. `TEAM KEY ACTIVE`
//        / `NOT SAVED` / `LOST ON REBOOT` is then three true sentences. ⇒ `active_unsaved`.
//      ⇒ EVERY arm reached BEFORE it has established NONE of that, so there is no active key to report and no team
//        to report it for. ⇒ `suppressed`. ⛔ A future arm added inside `receive` must be classified BY THIS RULE,
//        not by how it reads.
// ⚠ THE FOUR `active_unsaved` ARMS, EACH CHECKED AGAINST THAT RULE RATHER THAN GROUPED BY NAME:
//   · `record_unreadable` — the `/mrcfg` read failed. It sits AFTER re-check (3) (the read is the first thing past
//     it), so the live pair is present and it is our team's: active, and nothing was written. TRUE.
//   · `record_mismatch`   — the PERSISTED record names another team while the LIVE config names this one. The live
//     key is present and live membership matches the grant ⇒ active; the durable side refused ⇒ not saved, and the
//     five-term boot restore will not install it. TRUE on all three rows.
//   · `keyring_failed`    — includes `keyring_full` (P-15's loud refusal) and the store's I/O refusals. Live key
//     present, zero durable writes. TRUE.
//   · `binding_failed`    — the key DID land durably but the activation did not, so the next boot comes up KEYLESS.
//     `LOST ON REBOOT` is true in the sense that matters to the operator: it will not be there.
// ⛔ `default`-LESS, so an arm added to `GrantSave` and not CLASSIFIED is a `-Werror=switch` BUILD FAILURE — the
//    same fence `grant_save_name` carries, and the reason the sentinel's arm is spelled out instead.
inline GrantUiRoute grant_ui_route_of(GrantSave g) {
    switch (g) {
        case GrantSave::saved:             return GrantUiRoute::received;
        // ---- BEFORE re-check (3): ⛔ NOTHING IS ACTIVE, so ⛔ NOTHING IS SAID (the QG blocker, 2026-08-25) --------
        case GrantSave::zero_team:         return GrantUiRoute::suppressed;
        case GrantSave::not_our_team:      return GrantUiRoute::suppressed;
        case GrantSave::no_live_key:       return GrantUiRoute::suppressed;
        // ---- AFTER re-check (3): THE KEY **IS** LIVE; only durability failed --------------------------------------
        case GrantSave::record_unreadable: return GrantUiRoute::active_unsaved;
        case GrantSave::record_mismatch:   return GrantUiRoute::active_unsaved;
        case GrantSave::keyring_failed:    return GrantUiRoute::active_unsaved;
        case GrantSave::binding_failed:    return GrantUiRoute::active_unsaved;
        // ⛔ THE SENTINEL IS NOT AN OUTCOME AND GETS NO DOOR. `suppressed` is the FAIL-CLOSED answer (C2): a value
        //    that should be impossible may never talk its way onto the panel.
        case GrantSave::count:             return GrantUiRoute::suppressed;
    }
    return GrantUiRoute::suppressed;   // ⛔ unreachable; a REFUSAL, never a claim
}
struct GrantSaveResult {
    GrantSave  outcome = GrantSave::zero_team;
    KeyringErr err     = KeyringErr::none;   // meaningful on `keyring_failed`; ⛔ names a FACT, never material
};

// ================================================================ §UI-16 K6 — THE ONE FACT A ROUTE COULD NOT CARRY
// ★★★★ **THE QG BLOCKER OF 2026-08-25, AND IT IS A *LANDING*, ⛔ NEVER A WORD.** `grant_ui_route_of` above answers
//      WHICH DOOR a receipt takes, and it is right: a `keyring_failed` receipt IS `active_unsaved`, and its three
//      ruled rows (`TEAM KEY ACTIVE` / `NOT SAVED` / `LOST ON REBOOT`, S-26/S-27) are three TRUE sentences — the key
//      really is live in RAM and really will not survive a reboot. ⛔ **NOT ONE OF THOSE ROWS CHANGES.**
//      What the route could not carry is WHY the durable half refused, and exactly one refusal has somewhere to send
//      the operator: **a FULL keyring**. Spec §K6 (`:987`) rules the direction for *"a `KEYRING FULL` result"* —
//      ⛔ **either origin** — so the RECEIVED grant's acknowledgement must reach `SAVED KEYS` exactly as the
//      `team new` refusal's already does. Without this the fifth RECEIVED grant is a dead end with no way out.
// ★★★ `grant_ui_route_of` IS LEFT **BYTE-FOR-BYTE UNCHANGED** AND IS **CALLED** (U1), which is the whole shape of
//     this correction: ⛔ no enumerator is added to `GrantUiRoute`, so every landed `default`-less switch over it —
//     `fw_main`'s four arms, the probe's two replicas, the suite's totality sweep — keeps its cases and its meaning.
//     The new fact rides BESIDE the route, in a carrier, exactly as `GrantSaveResult` carries `{outcome, err}`.
// ⛔ IT IS A **TYPED** DERIVATION FROM `KeyringErr::keyring_full`, ⛔ never a text compare and ⛔ never "the store
//    looked full" re-read at the UI: the authority is the transaction's own error, reported once.
inline bool grant_ui_keyring_full(const GrantSaveResult& r) {
    return r.outcome == GrantSave::keyring_failed && r.err == KeyringErr::keyring_full;
}
// The two facts about ONE receipt, travelling together so they cannot be read apart (the `GrantSaveResult` shape).
// ⓘ `keyring_full` is ⛔ MEANINGFUL ONLY on the `active_unsaved` route — `grant_ui_keyring_full` can only answer
//   true for `keyring_failed`, which `grant_ui_route_of` sends there — so no other arm can carry it.
struct GrantUiVerdict {
    GrantUiRoute route        = GrantUiRoute::suppressed;   // FAIL-CLOSED default: say nothing (C2)
    bool         keyring_full = false;
};
inline GrantUiVerdict grant_ui_verdict_of(const GrantSaveResult& r) {
    GrantUiVerdict v{};
    v.route        = grant_ui_route_of(r.outcome);   // ★ the LANDED classifier, CALLED — ⛔ never re-spelled
    v.keyring_full = grant_ui_keyring_full(r);
    return v;
}

// ---- the service ------------------------------------------------------------------------------------------------
// RAM: two references. ⛔ No cached verdict and no state between calls — a receipt is a single transaction, so there
// is nothing to go stale and nothing holding a secret between calls (`TeamKeyringService`'s own rule, restated).
class TeamKeyGrantService {
  public:
    TeamKeyGrantService(TeamKeyringService& keyring, ITeamKeyBinding& binding)
        : _keyring(keyring), _binding(binding) {}

    // ★★★ THE ORDER IS THE CONTRACT, and it is the SAME order the provisioning transaction uses for the same reason
    //     (QG blocker 2): re-check -> **the KEY, durably, FIRST** -> then the ACTIVATION. A reboot that lands between
    //     the two finds a RETAINED record with no active binding, which comes up KEYLESS — the honest answer. The
    //     opposite order finds an ACTIVATION with no key behind it, which is a binding that lies.
    // ★ ZERO WRITES on every refusing path, and ZERO writes when nothing moved: a re-grant of IDENTICAL material
    //   costs `KeyringVerdict::unchanged` (the keyring's own counted guard) plus ⛔ no `commit_active` at all, because
    //   the record already witnesses exactly this key for exactly this team. That is K1's coalescing discipline
    //   applied to the SECOND record — counted by the fakes, ⛔ never argued.
    GrantSaveResult receive(const TeamKeyGrant& g) {
        GrantSaveResult r{};
        // (1) ⛔ 0 reads, 0 writes. It is refused HERE as well as in `put` deliberately: `put`'s refusal is the
        //     STORE's policy, this one is the HANDLER's, and neither may rely on the other having run.
        if (g.push_team_id == 0)              { r.outcome = GrantSave::zero_team;   return r; }
        // (2) THE LIVE MEMBERSHIP. ⛔ Not the granter's claim and not the record's — `Node` refused a foreign team at
        //     RX (`team_mismatch`, `lib/core/node.cpp:286` — verified 2026-08-24; the spec's `:258` had drifted),
        //     and this asks the same question again of the CURRENT config.
        if (g.push_team_id != g.live_team_id) { r.outcome = GrantSave::not_our_team; return r; }
        // (3) THE PAIR IS STILL LIVE. A switch between RX and drain wipes it; persisting a wiped pair would store 64
        //     zero bytes as if they were a team key, and the boot restore would then REJECT them for ever.
        if (!g.live_pub || !g.live_priv)      { r.outcome = GrantSave::no_live_key;  return r; }

        // (4) THE PERSISTED RECORD, WHICH IS A SECOND AUTHORITY. ⛔ Fails closed on an unreadable record.
        TeamKeyBinding cur{};
        if (!_binding.read(cur))                              { r.outcome = GrantSave::record_unreadable; return r; }
        if (cur.membership_team_id != g.push_team_id)         { r.outcome = GrantSave::record_mismatch;   return r; }

        // ★★ THE KEY, DURABLY, FIRST. `put` owns the whole write policy (one record per team, atomic replace,
        //    identical material writes nothing, a full keyring refuses loudly) — ⛔ nothing is re-decided here.
        const KeyringResult kr = _keyring.put(g.push_team_id, g.live_pub, g.live_priv);
        if (kr.verdict != KeyringVerdict::ok && kr.verdict != KeyringVerdict::unchanged) {
            r.outcome = GrantSave::keyring_failed; r.err = kr.err; return r;   // ⛔ the activation is NOT written
        }

        // ★ THEN THE ACTIVATION — and only when it would MOVE something (the zero-write guard, counted).
        if (!binding_current(cur, g)) {
            if (!_binding.commit_active(g.push_team_id, g.live_pub, g.live_priv)) {
                r.outcome = GrantSave::binding_failed; return r;
            }
        }
        r.outcome = GrantSave::saved;
        return r;
    }

  private:
    // Does `/mrcfg` ALREADY witness exactly this key for exactly this team, and say it is active? ★ All four terms
    // are the boot restore's terms (i), (ii)-as-binding, and (iv) — asked here so a re-grant of material the record
    // already holds spends ⛔ NO flash. ⛔ It is not "is a key present": a binding naming another team, or witnessing
    // a DIFFERENT public half, must be rewritten, not accepted.
    static bool binding_current(const TeamKeyBinding& cur, const TeamKeyGrant& g) {
        return cur.key_active && cur.binding_team_id == g.push_team_id
               && cur.committed_present && cur.committed_pub
               && memcmp(cur.committed_pub, g.live_pub, 32) == 0;
    }

    TeamKeyringService& _keyring;
    ITeamKeyBinding&    _binding;
};

}  // namespace mrfw
