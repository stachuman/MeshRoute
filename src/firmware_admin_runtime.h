// MeshRoute — src/firmware_admin_runtime.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ REMOTE-ADMIN v2 SLICE 5 — THE TARGET'S PREPARE / COMMIT / DISCARD LIVE-INSTALL SEAM, as a PURE header.
//
// Authority: design §6.5 ("candidate copy, full validation, durable save, then LIVE ACTIVATION … save failure
// leaves the old ACL and sessions active"), the Slice 5 brief §4.1, and R-RA-8 (accept = static + gateway).
//
// WHAT IT IS. Slice 3 gave the target two DURABLE records (`/mradmid`, `/mracl`) and deliberately installed
// nothing: "Design §6.2's residency question is SLICE 5's". This header is that answer's ORDERING half — the one
// place that decides WHEN a validated durable candidate becomes the node's RUNNING administration state.
//
// ⛔⛔ THE ORDER IS THE WHOLE CONTRACT, and every one of its four clauses is a rule a plausible implementation
//     gets wrong:
//       1. PREPARE FIRST, AND IT MAY FAIL. Everything fallible — the candidate's validation, the Ed25519
//          derivation, EVERY fresh epoch draw — happens BEFORE the durable save and mutates ⛔ NOTHING. A failed
//          preparation returns `runtime_unavailable` with ⛔ ZERO NV WRITES.
//          ⇒ the pre-check's simpler "just call install() after the save" shape is REFUSED here, because it
//            hides a fallible RNG draw AFTER the durable commit — the exact defect design §6.4 forbids.
//       2. A FAILED SAVE DISCARDS THE PLAN. The running pair, ACL, epochs, seen rows and ingress are preserved
//          EXACTLY. The plan is a per-call transient and its `SecretWipeGuard` scrubs it on that path too.
//       3. COMMIT IS NON-FAILING AND PRECEDES ANY SUCCESS LINE. By the time it runs there is nothing left that
//          can refuse, so a printed `ok` can never describe state that was not installed.
//       4. A NO-OP CHANGES NOTHING AT ALL. Identical material costs ⛔ zero draws, zero writes, zero reinstall
//          and zero invalidation — `AclService::commit_`'s byte comparison runs BEFORE the preparation.
//
// ⚠⚠ WHAT A SUCCESSFUL COMMIT DOES **NOT** PROMISE ([[B317]], restated because a console line must never claim
//    it): `mrnv::write_slot` on nRF52 is `remove()` THEN `open/write`, so a save that reports FAILURE does ⛔ NOT
//    promise the previous flash bytes survived. Clause 2 above is a statement about the RUNNING state — which
//    really is untouched — and ⛔ not about the medium.
//
// ⓘ WHAT THIS FILE DELIBERATELY DOES **NOT** HAVE (mark done-vs-missing in code):
//   · ⛔ NO CAPABILITY MACRO. The [[B255]] idiom: `MR_FEAT_RADMIN_ACCEPT` gates the INSTANTIATION in
//     `firmware_commands.cpp` / `fw_main.cpp`, so the native suite drives every arm here without declaring a
//     product role, and `tools/probe_features/ownership.py`'s census is unchanged by this file's arrival.
//   · ⛔ NO NV ACCESS, no store, no `Print`, no `Node`. The seam below is an interface; the firmware binds it to
//     the real `Node` and the native tests bind an explicit FAKE that counts draws and commits.
//   · ⛔ NO SESSION POLICY. Which requests authenticate, which verdict a retry earns and when a row expires all
//     live in `lib/core/remote_session.{h,cpp}`. This file only decides what is installed and when.
//   · ⛔ NO NEW NV LAYOUT, RECORD OR BACKEND, and ⛔ no new verb.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "device_nv.h"                // mrnv::AclBlob / AclRow / kAclSlots / kAclRole* — the durable carrier (U2)
#include "firmware_admin_acl.h"       // AclState + AclCensus + acl_census + the role values it already binds
#include "firmware_admin_identity.h"  // AdminIdState + SecretWipeGuard + the /mradmid content policy
#include "identity.h"                 // meshroute::Identity + identity_from_seed — the ONE derivation authority
#include "remote_session.h"           // meshroute::RemoteSessionInstall / AdminAclRow — the ONE install carrier

namespace mrfw {

// ★★★ THE BINDING, NOT A COINCIDENCE — the same shape `firmware_admin_acl.h` uses one file over. If the durable
//     record's row count and the core image's ever disagree, a slot number would mean two different rows.
static_assert(mrnv::kAclSlots == meshroute::kRadminAclSlots,
              "firmware_admin_runtime.h: the durable ACL's slot count and the live image's disagree");
// ⛔ THE ROLE BYTES ARE THE SAME BYTES. Slice 3's typed values ARE the image's; there is no translation table to
//    drift, and a mutation of either constant is caught here rather than at a controller months later.
static_assert(mrnv::kAclRoleEmpty    == meshroute::kRadminRoleEmpty
              && mrnv::kAclRoleOperator == meshroute::kRadminRoleOperator
              && mrnv::kAclRoleOwner    == meshroute::kRadminRoleOwner,
              "firmware_admin_runtime.h: the durable ACL roles and the live image's roles disagree");

// ---- the LIVE seam ------------------------------------------------------------------------------------------
// ★ AN INTERFACE AND NOT A `Node&`, for the reason every pure service in this directory takes one: the native
//   suite must be able to drive every ordering clause above with an explicit fake that COUNTS draws and commits
//   and can be told to refuse — and `platformio.ini`'s native env compiles no `src/*.cpp` at all (§B115), so a
//   decision left inside the firmware binding would have no automated cover whatsoever.
struct IAdminRuntime {
    virtual ~IAdminRuntime() = default;
    // ONE checked 64-bit epoch draw. `false` = no non-zero material arrived. ⛔ The implementation may NOT retry,
    // fall back to a clock/counter, cache entropy or return an unconditional `true` ([[B312]] stays open).
    virtual bool draw_epoch(uint64_t& out) = 0;
    // Install a PREPARED plan. ⛔ NON-FAILING by construction — it runs only after the durable save succeeded.
    virtual void commit(const meshroute::RemoteSessionInstall& plan) = 0;
    // ⛔ A COLD-BOOT ENTROPY REFUSAL: acceptance OFF, keys cleared, and ⛔ nothing durable touched.
    virtual void entropy_failed() = 0;
};

// ---- the ONE conversion path: the durable record -> the live image (U2) --------------------------------------
// ⛔ NEVER rebuilt field-by-field at a second site. An unoccupied or unrecognised row becomes CANONICAL ZERO
//   rather than being copied verbatim: a role byte the content policy does not accept must not reach the
//   image as a live row, and `acl_content_valid` has already refused any record that could contain one.
inline void admin_acl_image_from_blob(const mrnv::AclBlob& b,
                                      meshroute::AdminAclRow out[meshroute::kRadminAclSlots]) {
    for (uint8_t i = 0; i < meshroute::kRadminAclSlots; ++i) {
        out[i] = meshroute::AdminAclRow{};
        const mrnv::AclRow& r = b.rec[i];
        if (r.role != mrnv::kAclRoleOperator && r.role != mrnv::kAclRoleOwner) continue;   // hole -> canonical zero
        memcpy(out[i].ed_pub, r.ed_pub, 32);
        out[i].role = r.role;
    }
}

// True iff the two durable rows are the SAME live credential — the exact key AND the exact role. A role change
// IS a change: it moves what that slot may do, so its session must not survive it.
inline bool admin_acl_row_same(const mrnv::AclRow& a, const mrnv::AclRow& b) {
    return a.role == b.role && memcmp(a.ed_pub, b.ed_pub, 32) == 0;
}
inline bool admin_acl_row_live(const mrnv::AclRow& r) {
    return r.role == mrnv::kAclRoleOperator || r.role == mrnv::kAclRoleOwner;
}

// ---- PREPARE: an ACL mutation -------------------------------------------------------------------------------
// ★★ PER SLOT, AND ONLY WHERE SOMETHING ACTUALLY MOVED (design §6.5 / brief §4.1):
//      · UNCHANGED slot  -> `epoch_set[i] = false`. Its epoch and every seen/ingress row it owns stay EXACT.
//      · ADDED/CHANGED occupied slot -> a FRESH non-zero epoch, drawn HERE, before any write.
//      · REMOVED slot    -> epoch 0, i.e. UNUSABLE, and its work is invalidated at commit.
// ⛔ Returns `false` = `runtime_unavailable`: a needed draw refused, ⛔ nothing was mutated, and the caller must
//    NOT save. ⛔ There is no partial plan and no "install what we could".
[[nodiscard]] inline bool admin_runtime_prepare_acl(IAdminRuntime& rt, const mrnv::AclBlob& before,
                                                    const mrnv::AclBlob& after,
                                                    meshroute::RemoteSessionInstall& plan) {
    plan.set_acl = true;
    admin_acl_image_from_blob(after, plan.acl);
    for (uint8_t i = 0; i < meshroute::kRadminAclSlots; ++i) {
        if (admin_acl_row_same(before.rec[i], after.rec[i])) { plan.epoch_set[i] = false; continue; }
        plan.epoch_set[i] = true;
        if (!admin_acl_row_live(after.rec[i])) { plan.epoch[i] = 0; continue; }   // removed -> unusable
        uint64_t e = 0;
        if (!rt.draw_epoch(e)) return false;                                      // ⛔ refuse; nothing written
        plan.epoch[i] = e;
    }
    return true;
}

// ---- PREPARE: `acl reset confirm` ---------------------------------------------------------------------------
// ★ IT NEEDS NO ENTROPY AND THEREFORE CANNOT FAIL, and that is stated rather than left to look like an omission:
//   every slot becomes EMPTY, so every epoch becomes 0 (unusable) and there is nothing to draw for. The whole
//   session state goes with the rows it belonged to.
inline void admin_runtime_prepare_acl_reset(meshroute::RemoteSessionInstall& plan) {
    plan.set_acl = true;
    for (uint8_t i = 0; i < meshroute::kRadminAclSlots; ++i) {
        plan.acl[i]       = meshroute::AdminAclRow{};
        plan.epoch[i]     = 0;
        plan.epoch_set[i] = true;
    }
    plan.invalidate_all = true;
}

// ---- PREPARE: a ROOT mutation (generate / rotate / recover, and the boot install) -----------------------------
// ★★ TEN EPOCHS, AND A COMPLETE CANDIDATE BEFORE ANY READINESS IS PUBLISHED. A new administration pair changes
//    THIS TARGET's half of every base key, so every previously derived session key is dead by construction —
//    the ACL is PRESERVED (design §6.5: the controllers keep their rows and re-pin the new fingerprint) but no
//    old session may survive. ⛔ The epochs are RE-MINTED rather than cleared: clearing them would leave
//    `epoch == 0` on occupied slots, which this codebase treats as UNUSABLE, so a "cleared" node would look
//    provisioned and refuse everything (and a naive fix would run epoch 0, which §4.1 forbids outright).
// ⓘ TEN AND NOT "the occupied ones": this preparation has no ACL in hand by design — `AdminIdService` is
//   deliberately free of a second store dependency (its own §6.4 note) — and an epoch on an empty row is inert,
//   because readiness and every key selection read the ROW, never the epoch alone.
// ⛔ `false` = `runtime_unavailable`: nothing derived, nothing drawn that matters, ⛔ nothing written.
[[nodiscard]] inline bool admin_runtime_prepare_root(IAdminRuntime& rt, const uint8_t seed[32],
                                                     meshroute::RemoteSessionInstall& plan) {
    if (admin_buf_all_zero(seed, 32)) return false;      // ⛔ a dead seed never becomes a live root
    {
        meshroute::Identity id{};
        SecretWipeGuard<meshroute::Identity> g{id};       // 196 B of SECRET, for one derivation only
        meshroute::identity_from_seed(id, seed);
        memcpy(plan.x_secret, id.x_secret, 32);
        memcpy(plan.ed_pub,   id.ed_pub,   32);
    }
    plan.set_root       = true;
    plan.invalidate_all = true;
    for (uint8_t i = 0; i < meshroute::kRadminAclSlots; ++i) {
        uint64_t e = 0;
        if (!rt.draw_epoch(e)) return false;              // ⛔ an INCOMPLETE candidate is never published
        plan.epoch[i]     = e;
        plan.epoch_set[i] = true;
    }
    return true;
}

// ---- the BRIDGE: `IAdminLiveInstall` implemented over `IAdminRuntime` -----------------------------------------
// ★★★ IT OWNS THE ONE TRANSIENT PLAN, and that is why the two interfaces are not one. The SERVICE sees only an
//     ordering (`prepare` -> save -> `commit` | `discard`) and never a plan; the NODE sees only "draw an epoch"
//     and "install this". Neither can bypass the other, and the plan itself exists for exactly one call.
// ⛔ THE PLAN IS SCRUBBED ON EVERY EXIT — the discard path, the commit path and the DESTRUCTOR — because it
//    carries the administration X25519 secret. ⛔ It is ⛔ NOT a second resident image: this object lives on the
//    verb call's stack (or `setup()`'s), exactly like the `AclBlob` candidates beside it.
// ★ RE-PREPARING SCRUBS FIRST. A second `prepare_*` on the same object cannot inherit half of an earlier plan,
//   and a FAILED preparation leaves nothing armed — so a later `commit()` after a refusal installs NOTHING
//   rather than installing a partial plan.
class AdminLiveInstall final : public IAdminLiveInstall {
  public:
    explicit AdminLiveInstall(IAdminRuntime& rt) : _rt(rt) {}
    ~AdminLiveInstall() override { discard(); }
    AdminLiveInstall(const AdminLiveInstall&) = delete;
    AdminLiveInstall& operator=(const AdminLiveInstall&) = delete;

    [[nodiscard]] bool prepare_root(const uint8_t seed[32]) override {
        discard();
        if (!admin_runtime_prepare_root(_rt, seed, _plan)) { discard(); return false; }
        _armed = true;
        return true;
    }
    [[nodiscard]] bool prepare_acl(const mrnv::AclBlob& before, const mrnv::AclBlob& after) override {
        discard();
        if (!admin_runtime_prepare_acl(_rt, before, after, _plan)) { discard(); return false; }
        _armed = true;
        return true;
    }
    void prepare_acl_reset() override {
        discard();
        admin_runtime_prepare_acl_reset(_plan);
        _armed = true;
    }
    // ⛔ NON-FAILING. An unarmed object installs NOTHING — a `commit()` that follows a refused preparation is a
    //    no-op rather than a partial install.
    void commit() override {
        if (_armed) _rt.commit(_plan);
        discard();
    }
    void discard() override {
        crypto_wipe(&_plan, sizeof _plan);
        _plan  = meshroute::RemoteSessionInstall{};
        _armed = false;
    }
    [[nodiscard]] bool armed() const { return _armed; }   // test introspection only

  private:
    IAdminRuntime&                  _rt;
    meshroute::RemoteSessionInstall _plan{};
    bool                            _armed = false;
};

// ---- the BOOT install ----------------------------------------------------------------------------------------
// ★ IT READS NOTHING. Both records are classified and loaded by the caller (which owns NV), exactly as
//   `AclService::add` takes the identity state as a parameter. That is what makes all four boot outcomes
//   directly testable without a store.
// ⛔ AN EMPTY / UNREADABLE / UNSUPPORTED ROOT OR ACL DISABLES ACCEPTANCE, and it does so by INSTALLING THE
//    CLEARED STATE rather than by leaving whatever was there: there is no implicit provisioning, no fallback key
//    and ⛔ no invented active owner (design §6.4).
enum class AdminSessionBootState : uint8_t {
    disabled       = 0,   // absent / invalid / io_failed prerequisites, or no owner — the DETAIL is in the two
                          // existing per-record boot lines, which this one deliberately does not repeat
    ready          = 1,
    entropy_failed = 2,   // ⛔ a cold-boot draw refused: acceptance OFF, keys wiped, ⛔ NV untouched
};
struct AdminSessionBoot {
    AdminSessionBootState state = AdminSessionBootState::disabled;
    uint8_t               slots = 0;   // validated OCCUPIED rows, or 0
};

// ⛔⛔ THE TWO HALVES ARE INSTALLED **INDEPENDENTLY**, AND THAT IS [[B341]]'s FIX — QA's S5-Q1 gate finding,
//     reproduced before this file was changed. The first shape installed the CLEARED image whenever the
//     CONJUNCTION was false, i.e. it discarded the VALID half too; and because each verb prepares only its OWN
//     half (`AclService::commit_` sets `set_acl` and never the root; `AdminIdService::mint_` sets `set_root` and
//     never the ACL image), two ordinary sequences printed a DURABLE SUCCESS while the running node stayed
//     `disabled` until the next reboot — with no console line afterwards that would reveal it:
//       (B) `admin-id generate` -> reboot (ACL still absent) -> `acl add owner <key>` says
//           `> acl added slot=0 role=owner` while `root_present` is 0 and the node is not accepting;
//       (D) a valid owner ACL + a CORRUPT root record -> reboot -> `admin-id reset confirm` succeeds while
//           `acl_occupied` is 0 and the node is not accepting.
//     ⇒ THE PROPERTY THIS FUNCTION NOW OWNS: **after any sequence of durable successes, the running readiness
//       equals what a fresh boot on the same medium installs.** A valid half is installed; a bad half installs
//       ITS OWN cleared half (⛔ no stale live ACL, ⛔ no stale root), so the two never disagree.
// ⛔ DESIGN §6.4 IS UNCHANGED BY THE FIX: no implicit provisioning, no fallback key, no invented owner. A root
//    is installed only from a VALID `/mradmid`; an ACL image only from a VALID `/mracl` that already holds an
//    owner (`acl_content_valid` refuses an occupied-but-ownerless record outright, so an `ok` non-empty ACL
//    always has one — which is exactly why the per-half rule cannot make `remote_session_accepting` and
//    `admin_provisioning_ready` disagree).
inline AdminSessionBoot admin_runtime_boot(IAdminRuntime& rt, AdminIdState id_state, const uint8_t seed[32],
                                           AclState acl_state, const mrnv::AclBlob& acl) {
    AdminSessionBoot r;
    const AclCensus census = acl_census(acl);
    // ★ The two half-verdicts, named separately so neither can silently veto the other.
    const bool root_ok = (id_state == AdminIdState::ok);
    const bool acl_ok  = (acl_state == AclState::ok) && census.owners > 0;

    meshroute::RemoteSessionInstall plan{};
    SecretWipeGuard<meshroute::RemoteSessionInstall> g{plan};
    // ⛔ A BOOT IS A COLD START: whatever the block held, no session survives it.
    plan.invalidate_all = true;

    // ---- HALF 1: the administration ROOT --------------------------------------------------------------
    if (root_ok) {
        if (!admin_runtime_prepare_root(rt, seed, plan)) {
            // ⛔ KEEP ACCEPTANCE DISABLED, WIPE THE TEMPORARY KEYS, CHANGE NO NV. The operator sees the state on
            //    the boot line; the node simply does not answer remote administration until it can mint real
            //    epochs. ⛔ Nothing at all is installed — ⛔ not even the ACL half, because a node that cannot
            //    mint an epoch cannot hold a usable session for any row.
            rt.entropy_failed();
            r.state = AdminSessionBootState::entropy_failed;
            return r;
        }
    } else {
        plan.clear_root = true;
    }

    // ---- HALF 2: the live ACL image -------------------------------------------------------------------
    plan.set_acl = true;
    if (acl_ok) {
        admin_acl_image_from_blob(acl, plan.acl);
        for (uint8_t i = 0; i < meshroute::kRadminAclSlots; ++i) {
            if (plan.epoch_set[i]) continue;                 // the root half already minted one for this slot
            plan.epoch_set[i] = true;
            if (plan.acl[i].role == meshroute::kRadminRoleEmpty) { plan.epoch[i] = 0; continue; }
            uint64_t e = 0;
            if (!rt.draw_epoch(e)) {                         // ⛔ an INCOMPLETE candidate is never published
                rt.entropy_failed();
                r.state = AdminSessionBootState::entropy_failed;
                return r;
            }
            plan.epoch[i] = e;
        }
    } else {
        // ⛔ THE CLEARED ACL HALF — installed, ⛔ not merely left alone, so a node whose record went bad can
        //    never boot into a stale live ACL. ★ It ALSO overwrites any epochs the root half prepared: with no
        //    rows there is nothing for them to key, and a later `acl add` prepares its own.
        for (uint8_t i = 0; i < meshroute::kRadminAclSlots; ++i) {
            plan.acl[i]       = meshroute::AdminAclRow{};
            plan.epoch[i]     = 0;
            plan.epoch_set[i] = true;
        }
    }
    rt.commit(plan);

    // ⛔ THE REPORTED STATE IS THE SAME READ-ONLY CONJUNCTION THE VERBS USE (U1), recomputed rather than stored,
    //    so it can never outlive either record — and it now describes exactly what was installed.
    if (admin_provisioning_ready(id_state, acl_state, census)) {
        r.state = AdminSessionBootState::ready;
        r.slots = census.count;
    }
    return r;
}

inline const char* admin_session_boot_state_name(AdminSessionBootState s) {
    switch (s) {
        case AdminSessionBootState::ready:          return "ready";
        case AdminSessionBootState::entropy_failed: return "entropy_failed";
        case AdminSessionBootState::disabled:       break;
    }
    return "disabled";
}

}  // namespace mrfw
