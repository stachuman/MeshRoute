// MeshRoute — src/firmware_admin_acl.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ REMOTE-ADMIN v2 SLICE 3 — the TARGET-SIDE CONTROLLER ACL (`/mracl`), as a PURE service.
//
// Authority: design `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` §§6.4-6.6;
// rulings R-RA-6 / R-RA-8 / R-RA-21 / R-RA-29. Design §6.5, verbatim:
//
//     "Slot numbers are stable wire handles, not security identities. Empty entries are not compacted, an
//      ACL-full condition refuses loudly, and adding a duplicate public key is rejected."
//
// …and §6.6, verbatim:
//
//     "The last owner cannot be removed or demoted remotely.
//      A request cannot remove or demote its own authenticating slot. Rotation is add replacement owner, verify
//      a session through that owner, then remove the old entry.
//      ACL changes acknowledge success only after the new state is durable."
//
// ★★ THE TEN SLOTS ARE THE CODEC'S TEN. `lib/core/remote_codec.h:52`'s `kRemoteSlotSessionMax` (0x09) selects an
//    established authenticated session on the wire, and design §6.5 says the slot number IS that handle. ⇒ the two
//    tens are bound by a `static_assert` below, ⛔ never by coincidence. Including that declaration to read one
//    constant is ⛔ NOT becoming a runtime codec consumer: no codec function is called, this slice emits zero
//    remote events, and the 36-scenario corpus is inert by construction.
//
// ★★★ WHY IT IS A PURE HEADER: `test_build_src = no` — see the sibling identity header's note. Hoisted,
//     `test/test_firmware_admin_acl.cpp` drives every arm against a WRITE-COUNTING fake store, and
//     `--target=radmin3acl` can attack every rule individually.
// ⛔ NO CAPABILITY MACRO IN THIS FILE ([[B255]] idiom) — the gating is at the instantiation and the console surface.
//
// ⓘ WHAT THIS FILE DELIBERATELY DOES **NOT** HAVE (per [[meshroute-mark-done-vs-missing-in-code]]):
//   · ⛔ NO RESIDENT ACL, cache or `Node` member: each verb is one load-edit-store transaction, so there is no
//     state between calls to go stale. ⛔ No static I/O buffer either — the candidate is automatic, bounded scratch.
//   · ⛔ NO REMOTE CALLER AUTHORITY AND NO `CommandContext`. `AclActor` below carries an OPTIONAL acting slot so
//     that Slice 6's authenticated caller REUSES the self-slot rule rather than re-implementing it — but ⛔ the
//     LOCAL USB path supplies NONE, and the present-slot arms are tested here as FUTURE-CALLER SERVICE tests,
//     ⛔ never as executed remote requests. Deciding WHO may call which verb is Slice 6's, not this file's.
//   · ⛔ NO SESSION INVALIDATION on a removal — the session slice's. A removed row does not tear down anything
//     today because nothing establishes a session yet.
//   · ⛔ NO TWO-STORE ATOMIC COMMIT with `/mradmid`. "Provisioning ready" is a READ-ONLY CONJUNCTION (valid root
//     AND valid ACL AND at least one owner), computed on demand — ⛔ never an independently stored boolean, which
//     could outlive either half and claim a readiness neither record supports.
//
// ⚠⚠ THE LIMIT OF EVERY CLAIM HERE — identical to the identity store's, restated because a console line must not
//    over-promise: a save that reports FAILURE publishes no success and ⛔ does NOT promise the previous bytes
//    survived (nRF52 `write_slot` is remove-then-write); a whole-filesystem self-heal triggered by one of the six
//    OTHER probed files erases this record too ([[B317]], NOT closed here); and ⛔ there is no journal, witness
//    file, rollback write or automatic retry. The next command re-reads and re-classifies. Bench Part 55a owns the
//    real power-cut and physical-transport residue.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>                   // memcmp/memcpy — the byte-identical write guard and the row copy

#include "device_nv.h"                // mrnv::AclBlob / AclRow / AclRead / kAclSlots — THE durable carrier (U2)
#include "firmware_admin_identity.h"  // mrfw::admin_buf_all_zero + AdminIdState — the add gate's separately-read fact
#include "remote_codec.h"             // ★ ONE constant: meshroute::kRemoteSlotSessionMax. ⛔ No codec CALL.

namespace mrfw {

// ★★★ THE BINDING, NOT A COINCIDENCE. If the codec ever gains an eleventh session slot, or this record ever loses
//     a row, THIS line fails to compile — on every board, because `firmware_commands.cpp` includes this header
//     UNGATED. ⛔ Do not "fix" a future failure by editing the ten: the two numbers are one fact.
static_assert(mrnv::kAclSlots == meshroute::kRemoteSlotSessionMax + 1,
              "firmware_admin_acl.h: the ACL's slot count and the codec's session-slot handles disagree — "
              "design §6.5 makes them ONE number (remote_codec.h kRemoteSlotSessionMax)");

// ---- the role, as a FENCED enum ------------------------------------------------------------------------------
// ★ `-Werror=switch` is the reason this is an `enum class` and not a bare byte: this codebase has already shipped
//   three enum->string bugs that a byte-identity gate was structurally blind to
//   ([[meshroute-warnings-gate-blocking]]). Every mapping below is a `switch` with no `default`, so adding a role
//   without naming it will not compile.
// ⓘ `operator_` carries a trailing underscore because `operator` is a C++ keyword. The stored byte and the printed
//   token are both plain `operator`; the underscore never leaves this file's identifiers.
enum class AclRole : uint8_t {
    empty     = mrnv::kAclRoleEmpty,
    operator_ = mrnv::kAclRoleOperator,
    owner     = mrnv::kAclRoleOwner,
};
// ⛔ FAIL LOUD (C2): a byte outside the closed domain is REFUSED, never mapped to a default role. `empty` is a
//   legal STORED byte but is ⛔ never an assignable role — no verb may write it, so it is refused here too and the
//   only way a row becomes empty is `remove`.
inline bool acl_role_assignable(uint8_t b, AclRole& out) {
    if (b == mrnv::kAclRoleOperator) { out = AclRole::operator_; return true; }
    if (b == mrnv::kAclRoleOwner)    { out = AclRole::owner;     return true; }
    return false;
}
// The printed token. ⛔ `empty` has NO token: an empty row is never listed, so returning a name for it would be
//   inventing an output nothing produces.
inline const char* acl_role_name(AclRole r) {
    switch (r) {
        case AclRole::operator_: return "operator";
        case AclRole::owner:     return "owner";
        case AclRole::empty:     break;
    }
    return "";
}

// ---- the CONTENT policy (the `/mrui` split: LAYOUT is device_nv.h's, CONTENT is here) ------------------------
inline bool acl_row_occupied(const mrnv::AclRow& r) { return r.role != mrnv::kAclRoleEmpty; }

// ★★★ EVERY RULE THE RECORD MUST OBEY, IN ONE PREDICATE — and a record that breaks ANY of them is INVALID rather
//     than partially adopted. ⛔ NEVER clamp a count, compact a row or drop an offending entry: a half-understood
//     ACL is an authority list nobody reviewed, and the design's answer to a corrupt one is the confirm-gated
//     physical recovery verb, not a silent repair.
//       · `count` is the POPULATION and must equal it exactly — ⛔ not a high-water mark and ⛔ not an index.
//         "Empty entries are not compacted" (§6.5), so holes are legal and keep their slot numbers.
//       · an EMPTY row is all-zero: zero role, zero key, zero reserved. That is what `AclBlob{}` produces, and it
//         is what makes the whole-record `memcmp` write guard sound.
//       · an OCCUPIED row has a LEGAL role, a NON-ZERO key, and zero reserved bytes.
//       · occupied keys are PAIRWISE UNIQUE — "adding a duplicate public key is rejected" (§6.5) applies to the
//         stored state too, or a corrupt record could grant one controller two slots.
//       · ★ A NON-EMPTY ACL WITHOUT AN OWNER IS INVALID. An operator-only list can authorise nothing that matters
//         and can never be repaired remotely (the last-owner rule bites in reverse), so it is a corrupt state,
//         ⛔ not a legal one that happens to be useless.
inline bool acl_content_valid(const mrnv::AclBlob& b) {
    if (b.count > mrnv::kAclSlots) return false;
    uint16_t occupied = 0, owners = 0;
    for (uint8_t i = 0; i < mrnv::kAclSlots; ++i) {
        const mrnv::AclRow& r = b.rec[i];
        if (!admin_buf_all_zero(r.reserved, sizeof r.reserved)) return false;
        const bool zero_key = admin_buf_all_zero(r.ed_pub, sizeof r.ed_pub);
        if (r.role == mrnv::kAclRoleEmpty) {
            if (!zero_key) return false;                     // a "deleted" row that kept its key is NOT empty
            continue;
        }
        if (r.role != mrnv::kAclRoleOperator && r.role != mrnv::kAclRoleOwner) return false;
        if (zero_key) return false;                          // an occupied row with no key grants nothing to nobody
        ++occupied;
        if (r.role == mrnv::kAclRoleOwner) ++owners;
        for (uint8_t j = 0; j < i; ++j)
            if (acl_row_occupied(b.rec[j]) && !memcmp(b.rec[j].ed_pub, r.ed_pub, sizeof r.ed_pub)) return false;
    }
    if (b.count != occupied) return false;
    if (occupied > 0 && owners == 0) return false;
    return true;
}

// ---- the composed state --------------------------------------------------------------------------------------
enum class AclState : uint8_t {
    ok,        // a valid record — possibly EMPTY, which is an ordinary provisioned-but-unowned state
    absent,    // ★ NO RECORD — an un-provisioned node, ⛔ never an error
    invalid,   // ⛔ present but unusable: bad storage bytes, or content that breaks a rule above
    io_failed, // ⛔ the STORE would not answer — ⛔ NOTHING may be written, not even the recovery verb
};
inline AclState acl_state_of(mrnv::AclRead r, const mrnv::AclBlob& b) {
    switch (r) {
        case mrnv::AclRead::io_failed: return AclState::io_failed;
        case mrnv::AclRead::absent:    return AclState::absent;
        case mrnv::AclRead::invalid:   return AclState::invalid;
        case mrnv::AclRead::ok:        break;
    }
    return acl_content_valid(b) ? AclState::ok : AclState::invalid;
}

// ---- the census ------------------------------------------------------------------------------------------------
// ⛔ DERIVED FROM THE ROWS, ⛔ never from `count` alone: `count` is one of the things being validated, so trusting
//    it to describe the rows would make a corrupt record describe itself.
struct AclCensus { uint8_t count = 0, owners = 0, operators = 0; };
inline AclCensus acl_census(const mrnv::AclBlob& b) {
    AclCensus c;
    for (uint8_t i = 0; i < mrnv::kAclSlots; ++i) {
        const uint8_t role = b.rec[i].role;
        if (role == mrnv::kAclRoleOperator)  { ++c.count; ++c.operators; }
        else if (role == mrnv::kAclRoleOwner) { ++c.count; ++c.owners; }
    }
    return c;
}

// ---- the durable seam --------------------------------------------------------------------------------------
struct IAclStore {
    virtual ~IAclStore() = default;
    virtual mrnv::AclRead load(mrnv::AclBlob& out) = 0;
    virtual bool save(const mrnv::AclBlob& b) = 0;   // false = THE WRITE FAILED. ⛔ Never "nothing was written".
};

// ---- the OPTIONAL acting slot (design §6.6's "its own authenticating slot") ---------------------------------
// ★★ IT IS OPTIONAL BY CONSTRUCTION, and that is the whole point: a LOCAL USB operator has no ACL slot, so
//    `present` is FALSE on every path this slice ships. Slice 6's authenticated remote caller will set it, and the
//    rule it then obeys is the one tested here — ⛔ not a second copy written later beside the request handler.
struct AclActor {
    bool    present = false;
    uint8_t slot    = 0;
};

// ---- the typed verdicts ---------------------------------------------------------------------------------------
// ⓘ EXHAUSTIVE AND MINIMAL: every reason is REACHED by a native case. ⛔ No unreachable enumerator.
enum class AclErr : uint8_t {
    none,                  // success carries this
    bad_args,              // ⛔ a role or slot the service will not accept at all (the verb layer refuses first)
    store_invalid,         // the record is corrupt — only `acl reset confirm` may touch it
    store_io_failed,       // ⛔ the store would not answer: NOTHING is known, so NOTHING may be written
    not_invalid,           // recovery attempted on a record that is not corrupt (absent or ok)
    identity_absent,       // ★ the separately-read administration identity is missing …
    identity_invalid,      //   … corrupt …
    identity_io_failed,    //   … or unreadable. A first owner cannot be granted against a root that is not there.
    duplicate_key,         // that public key already holds a slot — ⛔ REFUSED, never "unchanged"
    zero_key,              // an all-zero key is not a key
    acl_full,              // ten occupied rows — ⛔ refuses LOUDLY and evicts NOTHING
    slot_empty,            // set/remove named a slot that holds no row
    first_owner_required,  // ★ the first credential on an empty ACL must be an OWNER, never an operator
    last_owner,            // ⛔ the last owner may not be removed or demoted
    self_slot,             // ⛔ a request may not remove or demote its own authenticating slot
    nv_save_failed,        // the durable write reported failure. ⛔ Does NOT promise the old bytes survived.
};
struct AclResult {
    bool    ok      = false;
    AclErr  err     = AclErr::none;
    uint8_t slot    = 0;
    AclRole role    = AclRole::empty;
    // ★ `false` on a legal SAME-ROLE assignment: identical material costs ⛔ ZERO writes, and the verb says
    //   `unchanged` rather than `updated` so an operator is never told a write happened that did not.
    bool    changed = false;
};
// The read-only boot line's facts. ⚠ For every NON-ok state the counters are ZERO, and they mean "NO ACCEPTED
// ROWS" — ⛔ never "the flash is empty". The state carries that distinction and the console must print it.
struct AclBoot {
    AclState  state = AclState::absent;
    AclCensus census;
};

// ---- the service ---------------------------------------------------------------------------------------------
// RAM: one reference. ⛔ No cached record, no draft, no member between calls.
// ⓘ ⛔ NO `SecretWipeGuard` IN THIS FILE, and its absence is a DECISION rather than an omission: `/mracl` holds
//   PUBLIC keys and role bytes only. There is no secret here to wipe, and a `crypto_wipe` of public material would
//   suggest one exists. The identity store — which does hold a secret — guards every one of its transients.
class AclService {
  public:
    explicit AclService(IAclStore& store) : _store(store) {}

    // ★ READ-ONLY. `out` is left as a VALID EMPTY record on every non-ok arm, so a caller can never print bytes
    //   from a partial read (`mrnv::load_acl` documents that `out` may hold one). ZERO writes on every arm.
    AclState read(mrnv::AclBlob& out) {
        const AclState s = acl_state_of(_store.load(out), out);
        if (s != AclState::ok) mrnv::acl_blob_init(out);
        return s;
    }
    AclBoot boot_report() {
        mrnv::AclBlob b{};
        AclBoot r;
        r.state = read(b);
        if (r.state == AclState::ok) r.census = acl_census(b);
        return r;
    }

    // ★★★ `acl add <operator|owner> <key>` — THE FIRST-OWNER CEREMONY AND EVERY LATER GRANT, one path.
    //     THE ORDER IS THE CONTRACT, and every refusal below costs ⛔ ZERO writes:
    //       reject the role/key -> require a valid administration ROOT -> load -> refuse unreadable -> seed an
    //       ABSENT record in RAM -> reject a duplicate -> require an OWNER first -> lowest FREE slot ->
    //       compose ONE candidate -> full validation -> AT MOST ONE save.
    //     ⇒ an ABSENT store costs EXACTLY ONE write (the seed and the row land together, ⛔ never two).
    // ★ `id_state` is SEPARATELY READ by the caller and passed in: design §6.4 forbids reporting provisioning
    //   success without a valid root, and taking the fact as a parameter keeps this service free of a second store
    //   dependency while making the rule directly testable in all four identity states.
    AclResult add(AdminIdState id_state, AclRole role, const uint8_t key[32]) {
        if (role != AclRole::operator_ && role != AclRole::owner) return fail_(AclErr::bad_args);
        if (admin_buf_all_zero(key, 32))                          return fail_(AclErr::zero_key);
        switch (id_state) {
            case AdminIdState::absent:    return fail_(AclErr::identity_absent);
            case AdminIdState::invalid:   return fail_(AclErr::identity_invalid);
            case AdminIdState::io_failed: return fail_(AclErr::identity_io_failed);
            case AdminIdState::ok:        break;
        }
        mrnv::AclBlob b{};
        const AclState s = acl_state_of(_store.load(b), b);
        if (s == AclState::io_failed) return fail_(AclErr::store_io_failed);
        if (s == AclState::invalid)   return fail_(AclErr::store_invalid);
        if (s == AclState::absent)    mrnv::acl_blob_init(b);   // seed in RAM — ONE write, below

        for (uint8_t i = 0; i < mrnv::kAclSlots; ++i)
            if (acl_row_occupied(b.rec[i]) && !memcmp(b.rec[i].ed_pub, key, 32)) return fail_(AclErr::duplicate_key);

        // ★ THE FIRST CREDENTIAL MUST BE AN OWNER. An operator-only ACL authorises nothing that matters and can
        //   never be promoted remotely, so allowing one would create a node nobody can manage and nobody can fix
        //   except physically. (It is also exactly what `acl_content_valid` calls corrupt.)
        const AclCensus c = acl_census(b);
        if (c.count == 0 && role != AclRole::owner) return fail_(AclErr::first_owner_required);

        // ⛔ THE LOWEST FREE SLOT, AND HOLES STAY HOLES: slot numbers are stable wire handles (§6.5), so an insert
        //    must never shift an existing row's number. A previously-removed slot 3 is reused before slot 7.
        int slot = -1;
        for (uint8_t i = 0; i < mrnv::kAclSlots; ++i) if (!acl_row_occupied(b.rec[i])) { slot = i; break; }
        if (slot < 0) return fail_(AclErr::acl_full);           // ⛔ evicts NOTHING, refuses LOUDLY

        mrnv::AclBlob cand = b;
        // ⛔ ZERO FIRST — `reserved` must be deterministic for the whole-record compare below.
        // ⓘ REDUNDANT BY CONSTRUCTION, AND SAID SO RATHER THAN LEFT TO LOOK LIKE COVERAGE (the
        //   `team_key_blob_init` idiom): the chosen slot is one `acl_row_occupied` called EMPTY, and
        //   `acl_content_valid` — which every loaded record has already passed — requires an empty row to be
        //   ALL-ZERO. ⇒ no reachable state makes this assignment change a byte, and ⛔ no mutation of it can
        //   redden the suite. It is kept as the second belt for a FUTURE caller that composes a row itself.
        cand.rec[slot] = mrnv::AclRow{};
        memcpy(cand.rec[slot].ed_pub, key, 32);
        cand.rec[slot].role = static_cast<uint8_t>(role);
        cand.count = static_cast<uint16_t>(c.count + 1);
        return commit_(b, cand, static_cast<uint8_t>(slot), role);
    }

    // `acl set <slot> <operator|owner>` — a role change on an OCCUPIED slot.
    AclResult set(uint8_t slot, AclRole role, AclActor actor = AclActor{}) {
        if (role != AclRole::operator_ && role != AclRole::owner) return fail_(AclErr::bad_args);
        if (slot >= mrnv::kAclSlots)                              return fail_(AclErr::bad_args);
        mrnv::AclBlob b{};
        const AclState s = acl_state_of(_store.load(b), b);
        if (s == AclState::io_failed) return fail_(AclErr::store_io_failed);
        if (s == AclState::invalid)   return fail_(AclErr::store_invalid);
        if (s == AclState::absent || !acl_row_occupied(b.rec[slot])) return fail_(AclErr::slot_empty);

        const uint8_t cur_byte = b.rec[slot].role;
        // ★★ THE THREE-CHECK ORDER, AND IT IS DELIBERATE:
        //   1. SELF-DEMOTION first — it is a fact about the REQUESTER, so it is refused before the record is
        //      consulted for anything else. ⛔ A same-role assignment is NOT a demotion and stays legal below;
        //      a self PROMOTION is not a demotion either and is left to Slice 6's authority rules, ⛔ not silently
        //      forbidden here under a reason that does not describe it.
        if (actor.present && actor.slot == slot &&
            cur_byte == mrnv::kAclRoleOwner && role == AclRole::operator_) return fail_(AclErr::self_slot);
        //   2. THE NO-OP — identical material costs ⛔ ZERO writes and says `unchanged`, never `updated`.
        if (cur_byte == static_cast<uint8_t>(role)) {
            AclResult r; r.ok = true; r.slot = slot; r.role = role; r.changed = false; return r;
        }
        //   3. THE LAST OWNER may not be demoted — by ANY caller, including this local physical one. Recovering a
        //      node whose only owner was demoted would otherwise need a factory reset.
        const AclCensus c = acl_census(b);
        if (cur_byte == mrnv::kAclRoleOwner && c.owners == 1) return fail_(AclErr::last_owner);

        mrnv::AclBlob cand = b;
        cand.rec[slot].role = static_cast<uint8_t>(role);       // ⛔ population unchanged -> `count` unchanged
        return commit_(b, cand, slot, role);
    }

    // `acl remove <slot> confirm` — free one slot, leaving a HOLE.
    AclResult remove(uint8_t slot, AclActor actor = AclActor{}) {
        if (slot >= mrnv::kAclSlots) return fail_(AclErr::bad_args);
        mrnv::AclBlob b{};
        const AclState s = acl_state_of(_store.load(b), b);
        if (s == AclState::io_failed) return fail_(AclErr::store_io_failed);
        if (s == AclState::invalid)   return fail_(AclErr::store_invalid);
        if (s == AclState::absent || !acl_row_occupied(b.rec[slot])) return fail_(AclErr::slot_empty);

        if (actor.present && actor.slot == slot) return fail_(AclErr::self_slot);
        const AclCensus c = acl_census(b);
        // ⛔ INCLUDING WHEN IT IS THE ONLY ROW. "The last owner cannot be removed" has no population exception: a
        //    node cannot be emptied of authority by this verb, and `factory_reset confirm` is the verb that clears
        //    a node completely. (Design §6.6; the same rule is what `acl_content_valid` enforces on stored bytes.)
        if (b.rec[slot].role == mrnv::kAclRoleOwner && c.owners == 1) return fail_(AclErr::last_owner);

        mrnv::AclBlob cand = b;
        cand.rec[slot] = mrnv::AclRow{};                        // ⛔ ZEROED IN PLACE — never compacted (§6.5)
        cand.count = static_cast<uint16_t>(c.count - 1);
        return commit_(b, cand, slot, AclRole::empty);
    }

    // ★★ `acl reset confirm` — the ONLY write permitted over an INVALID record, and the sole exception to "an
    //    unreadable store costs zero writes". ⛔ `io_failed` refuses even here: nothing is known, so a re-init
    //    would destroy up to ten intact rows because a mount failed transiently. ⛔ `absent`/`ok` are
    //    `not_invalid` — there is nothing to recover.
    // ⛔ IT REINITIALISES TO AN EMPTY, VALID RECORD AND STOPS. It grants nobody: the operator then runs the
    //    ordinary `acl add owner …` physically. A recovery that invented an owner would be exactly what design
    //    §6.4 forbids.
    AclResult recover() {
        mrnv::AclBlob b{};
        const AclState s = acl_state_of(_store.load(b), b);
        if (s == AclState::io_failed) return fail_(AclErr::store_io_failed);
        if (s != AclState::invalid)   return fail_(AclErr::not_invalid);
        mrnv::AclBlob cand{};
        mrnv::acl_blob_init(cand);
        if (!_store.save(cand)) return fail_(AclErr::nv_save_failed);
        AclResult r; r.ok = true; r.changed = true; return r;
    }

  private:
    static AclResult fail_(AclErr e) { AclResult r; r.err = e; return r; }

    // ★★★ THE ONE COMMIT PATH (U2): full validation -> byte comparison -> AT MOST ONE save. Every mutation above
    //     reaches it, so there is ⛔ no second place a candidate can be written.
    //   · The validation is ⛔ NOT decoration: it is what makes "a partially valid ACL is never adopted" true of
    //     the bytes this service itself composes, not only of the bytes it reads.
    //   · The byte comparison is the write guard `save_acl` deliberately does NOT carry (see device_nv.h): a
    //     candidate identical to the stored record costs ZERO writes. `set`'s same-role arm returns before it, so
    //     this comparison is the SECOND line of that defence rather than its only one.
    AclResult commit_(const mrnv::AclBlob& before, const mrnv::AclBlob& cand, uint8_t slot, AclRole role) {
        // ⓘ REDUNDANT BY CONSTRUCTION FOR THIS SERVICE'S OWN CANDIDATES, and said so: every mutation above
        //   starts from a record that already passed `acl_content_valid` and changes it in exactly one way the
        //   rules permit — an owner is never the last one when demoted or removed (checked above), a new row is
        //   never a duplicate or zero-keyed (checked above), and `count` is moved with the population. ⇒ no
        //   reachable path reaches this line with an invalid candidate, and ⛔ no mutation of it can redden the
        //   suite. It is kept because it is what makes *"a partially valid ACL is never adopted"* true of the
        //   bytes this service COMPOSES and not only of the bytes it READS — the belt a future caller will need.
        if (!acl_content_valid(cand)) return fail_(AclErr::store_invalid);
        AclResult r;
        r.slot = slot;
        r.role = role;
        if (!memcmp(&cand, &before, sizeof cand)) { r.ok = true; r.changed = false; return r; }
        if (!_store.save(cand)) return fail_(AclErr::nv_save_failed);
        r.ok = true;
        r.changed = true;
        return r;
    }

    IAclStore& _store;
};

// ---- provisioning readiness: a READ-ONLY CONJUNCTION, ⛔ never a stored flag --------------------------------
// ★ Design §6.4: *"Provisioning reports success only when the required target-identity and ACL records are valid
//   and durable."* ⇒ readiness is recomputed from the two records every time it is asked, so it can never outlive
//   either of them. ⛔ There is no atomic two-store commit and no `provisioned` boolean anywhere in this slice.
inline bool admin_provisioning_ready(AdminIdState id_state, AclState acl_state, const AclCensus& c) {
    return id_state == AdminIdState::ok && acl_state == AclState::ok && c.owners > 0;
}

}  // namespace mrfw
