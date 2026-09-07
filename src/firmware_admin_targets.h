// MeshRoute — src/firmware_admin_targets.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ REMOTE-ADMIN v2 SLICE 4 — the CONTROLLER-SIDE TARGET BOOK (`/mrtargets`), as a PURE service.
//
// Authority: design `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` §§6.2-6.4;
// rulings R-RA-12 (GLOBAL is implicit), R-RA-28 (the reserved carrier capacities, unchanged here), R-RA-29 (the
// frozen fingerprint) and R-RA-30 (public list/show may cross secured BLE).
//
// WHAT IT IS. THIRTY-TWO rows, each pairing ONE managed node's IMMUTABLE administration public key with the
// operator's REPLACEABLE routing metadata — a `key_hash32` hint, an optional 1..3-hop destination layer path and a
// short unique label. Six operations — list, show, add, set, remove — plus a confirm-gated `reset` of a CORRUPT
// record and a READ-ONLY boot report. Nothing else.
//
// ★★★ THE TRUST/ROUTING SPLIT IS THE WHOLE POINT OF THIS FILE, and it is the lesson
//     [[meshroute-id-to-hash-trust-model]] records: `admin_pub` is the trust identity and it is IMMUTABLE and
//     UNIQUE; `key_hash32` is a ROUTING INSTRUCTION (`lib/core/identity.h:40`: "NOT a security anchor") and it is
//     REPLACEABLE metadata. ⇒ ⛔ two rows carrying the SAME hash are still TWO principals and must never merge;
//     ⛔ the hash is never derived from the key as a substitute for the operator's hint; ⛔ a fingerprint is a
//     DISPLAY/SELECTION handle and never an authorisation.
//
// ★★★ WHY IT IS A PURE HEADER: `test_build_src = no` — see the sibling keyring's note. Hoisted,
//     `test/test_firmware_admin_targets.cpp` drives every arm against a WRITE-COUNTING fake store and
//     `--target=radmin4targets` can attack every rule individually.
// ⛔ NO CAPABILITY MACRO IN THIS FILE ([[B255]] idiom) — the gating is at the instantiation and the console surface.
//
// ⓘ WHAT THIS FILE DELIBERATELY DOES **NOT** HAVE (per [[meshroute-mark-done-vs-missing-in-code]]):
//   · ⛔ NO OWNED STORAGE. The 2056-byte `TargetBlob` is the CALLER'S buffer, passed in per call — on a CLIENT
//     board that is the ONE resident scratch in `firmware_commands.cpp` (design §6.2's residency decision, whose
//     RAM cost this slice attributes), and in the native suite it is a stack blob. ⛔ No reference is retained
//     between calls, so this is IO/candidate scratch and ⛔ never a live authority cache: every entry point
//     RELOADS and RECLASSIFIES, and every failing mutation restores the row it touched before returning.
//   · ⛔ NO REACHABILITY CLAIM. A stored path is what the OPERATOR said; whether the target is actually reachable —
//     source/home/path validity and R-RA-28's caps — is Slice 8b's question, AT SEND TIME. Storage proves nothing.
//   · ⛔ NO SESSION, RPC, CODEC CALL OR `Node` MEMBER. Zero remote events; the corpus is inert by construction.
//   · ⛔ NO GLOBAL/BROADCAST ROW: GLOBAL is IMPLICIT (R-RA-12) and inventing a stored row for it would create a
//     principal the design does not have.
//   · ⛔ NO EVICTION AND NO COMPACTION: a full book refuses LOUDLY (design §6.3 forbids the `/mrpeers` behaviour
//     here), and a removed slot stays a hole so slot numbers remain stable handles.
//
// ⚠⚠ THE LIMIT OF EVERY CLAIM HERE — identical to the keyring's: ⛔ NO transaction and NO atomicity (nRF52
//    `write_slot` is remove-then-write), so a save that reports FAILURE publishes no success and ⛔ never says
//    "nothing was written"; and a whole-filesystem self-heal triggered by one of the SIX probed files erases this
//    record too ([[B317]], NOT closed here).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>                  // memcmp/memcpy — the byte-identical write guard and the row copy

#include "device_nv.h"               // mrnv::TargetBlob / TargetRow / TargetRead — THE durable carrier (U2)
#include "firmware_admin_identity.h" // ★ mrfw::admin_buf_all_zero + admin_fp_hex + the hex renderers — REUSED (U1)

namespace mrfw {

// ---- the CONTENT policy (the `/mrui` split: LAYOUT is device_nv.h's, CONTENT is here) ------------------------
inline bool target_row_occupied(const mrnv::TargetRow& r) { return (r.flags & mrnv::kTargetFlagOccupied) != 0; }

// ★ THE LABEL ALPHABET, NAMED ONCE. `[A-Za-z0-9_.-]`, CASE-SENSITIVE, 1..16 bytes. ⛔ No spaces and no escapes,
//   which is what makes every listing line a fixed-shape token stream that needs no quoting — the property the
//   output bound in `firmware_admin_client_verbs.h` depends on.
inline bool target_label_char_ok(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '.' || c == '-';
}
inline bool target_label_valid(const char* p, uint8_t len) {
    if (len == 0 || len > sizeof(mrnv::TargetRow::label)) return false;
    for (uint8_t i = 0; i < len; ++i) if (!target_label_char_ok(p[i])) return false;
    return true;
}
// ⛔ THE UNUSED TAIL MUST BE ZERO — the whole-record byte compare that decides whether flash is written at all must
//    be deterministic, and a label shortened from `alpha` to `al` that kept `pha` would compare unequal for ever.
inline bool target_label_tail_zero(const mrnv::TargetRow& r) {
    for (size_t i = r.label_len; i < sizeof r.label; ++i) if (r.label[i] != '\0') return false;
    return true;
}
// ★ THE PATH: `hop_count == 0` means SAME LAYER and every hop byte is zero; 1..3 means that many NON-ZERO
//   destination layer ids followed by a ZERO tail. ⛔ There is no fourth destination (`hops[4]` is the carrier's
//   array width; the carrier PREPENDS our own layer), ⛔ no clamp and ⛔ no automatic repair.
inline bool target_path_valid(const mrnv::TargetRow& r) {
    if (r.hop_count > mrnv::kTargetHopMax) return false;
    for (uint8_t i = 0; i < r.hop_count; ++i)          if (r.hops[i] == 0) return false;
    for (size_t i = r.hop_count; i < sizeof r.hops; ++i) if (r.hops[i] != 0) return false;
    return true;
}

// ★★★ EVERY RULE THE RECORD MUST OBEY, IN ONE PREDICATE — a record that breaks ANY of them is INVALID rather than
//     partially adopted. ⛔ NEVER clamp, compact or drop an offending row.
//       · `count` is the POPULATION and must equal it exactly.
//       · an EMPTY row is ALL 64 BYTES ZERO. That is what `TargetBlob{}` produces and what makes the whole-record
//         compare sound.
//       · an OCCUPIED row has `flags` EXACTLY `kTargetFlagOccupied` (⛔ no other bit — an unknown bit is a CORRUPT
//         record, never a silently-ignored one), a NON-ZERO administration key, a NON-ZERO routing hash, a
//         canonical label, a canonical path and ZERO reserved bytes.
//       · occupied administration keys are PAIRWISE UNIQUE — the stored form of "the full public key is immutable
//         and unique", or a corrupt record could give one principal two rows.
//       · occupied labels are PAIRWISE UNIQUE and CASE-SENSITIVE — the selector `label=` must resolve to at most
//         one row, and a stored duplicate would make that impossible.
//       · ⛔ `key_hash32` is NOT required unique: two managed nodes may legitimately share a routing hint, and
//         merging them would be exactly the id→hash trust confusion this codebase has a memory entry about.
inline bool target_content_valid(const mrnv::TargetBlob& b) {
    if (b.count > mrnv::kTargetSlots) return false;
    uint16_t occupied = 0;
    for (uint8_t i = 0; i < mrnv::kTargetSlots; ++i) {
        const mrnv::TargetRow& r = b.rec[i];
        if (!target_row_occupied(r)) {
            // ⛔ An "emptied" row that kept ANY byte is NOT empty. One whole-row zero test, so no field can be
            //    forgotten here the way a field-by-field list would eventually forget one.
            if (!admin_buf_all_zero(reinterpret_cast<const uint8_t*>(&r), sizeof r)) return false;
            continue;
        }
        if (r.flags != mrnv::kTargetFlagOccupied)                    return false;
        if (!admin_buf_all_zero(r.reserved, sizeof r.reserved))      return false;
        if (admin_buf_all_zero(r.admin_pub, sizeof r.admin_pub))     return false;
        if (r.key_hash32 == 0)                                       return false;   // 0 = "unset" everywhere here
        if (!target_label_valid(r.label, r.label_len))               return false;
        if (!target_label_tail_zero(r))                              return false;
        if (!target_path_valid(r))                                   return false;
        ++occupied;
        for (uint8_t j = 0; j < i; ++j) {
            const mrnv::TargetRow& o = b.rec[j];
            if (!target_row_occupied(o)) continue;
            if (!memcmp(o.admin_pub, r.admin_pub, sizeof r.admin_pub)) return false;   // one principal, two rows
            if (o.label_len == r.label_len && !memcmp(o.label, r.label, r.label_len)) return false;
        }
    }
    return b.count == occupied;
}

// ---- the composed state --------------------------------------------------------------------------------------
enum class TargetState : uint8_t {
    ok,        // a valid record — possibly EMPTY, which is an ordinary un-provisioned controller
    absent,    // ★ NO RECORD — ⛔ never an error
    invalid,   // ⛔ present but unusable: bad storage bytes, or content that breaks a rule above
    io_failed, // ⛔ the STORE would not answer — ⛔ NOTHING may be written, not even the recovery verb
};
inline TargetState target_state_of(mrnv::TargetRead r, const mrnv::TargetBlob& b) {
    switch (r) {
        case mrnv::TargetRead::io_failed: return TargetState::io_failed;
        case mrnv::TargetRead::absent:    return TargetState::absent;
        case mrnv::TargetRead::invalid:   return TargetState::invalid;
        case mrnv::TargetRead::ok:        break;
    }
    return target_content_valid(b) ? TargetState::ok : TargetState::invalid;
}
inline uint8_t target_population(const mrnv::TargetBlob& b) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < mrnv::kTargetSlots; ++i) if (target_row_occupied(b.rec[i])) ++n;
    return n;
}

// ---- SELECTOR RESOLUTION, split so its `ambiguous` arm is REACHABLE and TESTABLE ----------------------------
// ★★★ WHY IT IS TWO FUNCTIONS AND NOT ONE. The `fp=` selector's ambiguous arm needs TWO occupied rows whose
//     BLAKE2b-512 fingerprints agree while their 32-byte keys differ — i.e. a real digest collision, which cannot
//     be constructed. A single fused resolver would therefore contain a branch NO test could enter, and an
//     unreachable branch is an untested claim ([[the "seventeen green instruments" shape]]). ⇒ matching produces a
//     BIT MASK, and the mask→verdict step is a pure free function a native case drives with SYNTHETIC masks. That
//     is labelled here, in as many words: the `many` verdict below is exercised from a synthetic mask and ⛔ NO
//     claim is made anywhere that a BLAKE2b collision was produced.
// ⛔ AND IT IS `many` RATHER THAN FIRST-MATCH BY RULING: an ambiguous selector REFUSES (C2). A `label=` collision
//    cannot occur in a VALID record either (labels are pairwise unique), so the same helper serves both selectors
//    and the same synthetic-mask case covers both.
enum class TargetPick : uint8_t { none, one, many };
inline TargetPick target_pick_from_mask(uint32_t mask, uint8_t& slot) {
    if (mask == 0) return TargetPick::none;
    uint8_t first = 0, n = 0;
    for (uint8_t i = 0; i < mrnv::kTargetSlots; ++i)
        if (mask & (static_cast<uint32_t>(1) << i)) { if (n++ == 0) first = i; }
    if (n > 1) return TargetPick::many;
    slot = first;
    return TargetPick::one;
}
// The `label=` match mask over OCCUPIED rows. Case-sensitive, whole-label, ⛔ never a prefix.
inline uint32_t target_mask_by_label(const mrnv::TargetBlob& b, const char* label, uint8_t len) {
    uint32_t m = 0;
    if (len == 0 || len > sizeof(mrnv::TargetRow::label)) return 0;
    for (uint8_t i = 0; i < mrnv::kTargetSlots; ++i) {
        const mrnv::TargetRow& r = b.rec[i];
        if (!target_row_occupied(r) || r.label_len != len) continue;
        if (!memcmp(r.label, label, len)) m |= (static_cast<uint32_t>(1) << i);
    }
    return m;
}
// The `fp=` match mask over OCCUPIED rows — R-RA-29's fingerprint, computed from the stored key, compared as the
// 16 canonical lowercase hex characters the operator typed. ⛔ The fingerprint is a SELECTION handle and ⛔ never
// an authorisation: it selects the row whose FULL key then governs everything.
inline uint32_t target_mask_by_fp(const mrnv::TargetBlob& b, const char fp_lower[kAdminFpHex]) {
    uint32_t m = 0;
    for (uint8_t i = 0; i < mrnv::kTargetSlots; ++i) {
        const mrnv::TargetRow& r = b.rec[i];
        if (!target_row_occupied(r)) continue;
        char have[kAdminFpHex + 1];
        admin_fp_hex(r.admin_pub, have);
        if (!memcmp(have, fp_lower, kAdminFpHex)) m |= (static_cast<uint32_t>(1) << i);
    }
    return m;
}

// ---- the durable seam --------------------------------------------------------------------------------------
struct ITargetStore {
    virtual ~ITargetStore() = default;
    virtual mrnv::TargetRead load(mrnv::TargetBlob& out) = 0;
    virtual bool save(const mrnv::TargetBlob& b) = 0;   // false = THE WRITE FAILED. ⛔ Never "nothing was written".
};

// ---- the FUTURE-CALLER in-use predicate ---------------------------------------------------------------------
// ★★ The book's twin of the keyring's: a target an OPEN SESSION or a pending request is bound to must not be
//    removed or re-keyed underneath it. ⛔ Slice 4 has NO producer (Slice 8a is the first), so the PRODUCTION
//    binding answers "nothing is in use" and every busy-predicate test is a **future-caller service test**.
struct ITargetUse {
    virtual ~ITargetUse() = default;
    virtual bool slot_in_use(uint8_t slot) const = 0;
    virtual bool any_in_use() const = 0;
};

// ---- the typed verdicts ------------------------------------------------------------------------------------
// ⓘ EXHAUSTIVE AND MINIMAL: every reason is REACHED by a native case. ⛔ No unreachable enumerator.
enum class TargetErr : uint8_t {
    none,
    bad_args,         // ⛔ a subcommand, selector, label, key, hash or path this family does not accept
    needs_confirm,    // ⛔ a destructive verb reached without its EXACT `confirm` token — ZERO writes
    store_invalid,    // the record is present and corrupt — only the confirm-gated reset may touch it
    store_io_failed,  // ⛔ the store would not answer: NOTHING is known, so NOTHING may be written
    not_invalid,      // reset attempted on a record that is not corrupt (absent or ok)
    not_found,        // the selector resolved to no occupied row
    ambiguous,        // ⛔ the selector resolved to MORE THAN ONE row — refuse, ⛔ never first-match
    duplicate,        // that administration key, or that label, already holds a row
    full,             // all 32 rows are occupied — ⛔ refuses LOUDLY and evicts NOTHING
    in_use,           // ⛔ a live caller is bound to this target (or to some target, for reset)
    unchanged,        // ★ a legal `set` whose three fields are already the stored ones — ZERO writes, ⛔ never a
                      //   reported success, so an operator is never told a write happened that did not
    nv_save_failed,   // the durable write reported failure. ⛔ Does NOT promise the old bytes survived.
};
struct TargetResult {
    bool      ok   = false;
    TargetErr err  = TargetErr::none;
    uint8_t   slot = 0;
    uint8_t   admin_pub[32] = {};   // PUBLIC material, populated on success alone
};
// The read-only boot line's facts. ⚠ For every NON-ok state `count` is ZERO and means "NO ACCEPTED ROWS" —
// ⛔ never "the flash is empty". The state carries that distinction and the console prints it first.
struct TargetBoot {
    TargetState state = TargetState::absent;
    uint8_t     count = 0;
};

// ★ The candidate a mutation composes, assembled by the VERB from fully-parsed arguments and handed here as ONE
//   value. ⛔ It is not a partially-filled row that the service completes: parse-then-validate-then-construct is
//   the order, so nothing half-parsed can ever reach the record.
struct TargetSpec {
    uint8_t  admin_pub[32] = {};
    uint32_t key_hash32    = 0;
    uint8_t  hops[4]       = {};
    uint8_t  hop_count     = 0;
    char     label[16]     = {};
    uint8_t  label_len     = 0;
};

// ---- the service ---------------------------------------------------------------------------------------------
// RAM: two references. ⛔ No cached record, no draft, no member between calls — the 2056-byte book is the CALLER's
// buffer and is reloaded and reclassified on EVERY entry point.
// ⓘ ⛔ NO `SecretWipeGuard` IN THIS FILE, and its absence is a DECISION rather than an omission: `/mrtargets` holds
//   PUBLIC keys, a routing hint, a label and a path. There is no secret here to wipe, and a `crypto_wipe` of public
//   material would suggest one exists. The KEYRING — which does hold secrets — guards every one of its transients.
class TargetService {
  public:
    TargetService(ITargetStore& store, const ITargetUse& use) : _store(store), _use(use) {}

    // ★ READ-ONLY. `book` is left as a VALID EMPTY record on every non-ok arm, so a caller can never print bytes
    //   from a partial read (`mrnv::load_targets` documents that `out` may hold one). ZERO writes on every arm.
    //   ⛔ THE CALLER OWNS `book`; this service keeps no reference to it after the call returns.
    TargetState read(mrnv::TargetBlob& book) {
        const TargetState s = target_state_of(_store.load(book), book);
        if (s != TargetState::ok) mrnv::target_blob_init(book);
        return s;
    }
    TargetBoot boot_report(mrnv::TargetBlob& book) {
        TargetBoot r;
        r.state = read(book);
        if (r.state == TargetState::ok) r.count = target_population(book);
        mrnv::target_blob_init(book);   // ⛔ the scratch is INVALIDATED before it can be mistaken for a live cache
        return r;
    }

    // ★★★ `admin-target add <LABEL> <key> hash=… [layer=…]` — THE LOWEST FREE PHYSICAL ROW, and holes are reused
    //     before fresh slots so a removed slot 3 is filled before slot 7. ⛔ Slot numbers never shift.
    //     THE ORDER IS THE CONTRACT, and every refusal costs ⛔ ZERO writes:
    //       validate the spec -> load -> refuse unreadable/corrupt -> seed an ABSENT record in RAM -> refuse a
    //       duplicate key or label -> lowest free row -> compose IN THE CALLER'S BUFFER -> validate the whole book
    //       -> AT MOST ONE save -> publish. A failed save RESTORES the row and the count before returning.
    TargetResult add(mrnv::TargetBlob& book, const TargetSpec& spec) {
        if (!spec_valid_(spec)) return fail_(TargetErr::bad_args);
        const TargetState s = load_for_write_(book);
        if (s == TargetState::io_failed) return fail_(TargetErr::store_io_failed);
        if (s == TargetState::invalid)   return fail_(TargetErr::store_invalid);

        for (uint8_t i = 0; i < mrnv::kTargetSlots; ++i) {
            const mrnv::TargetRow& r = book.rec[i];
            if (!target_row_occupied(r)) continue;
            if (!memcmp(r.admin_pub, spec.admin_pub, sizeof r.admin_pub)) return fail_(TargetErr::duplicate);
            if (r.label_len == spec.label_len && !memcmp(r.label, spec.label, spec.label_len))
                return fail_(TargetErr::duplicate);
        }
        int slot = -1;
        for (uint8_t i = 0; i < mrnv::kTargetSlots; ++i) if (!target_row_occupied(book.rec[i])) { slot = i; break; }
        if (slot < 0) return fail_(TargetErr::full);          // ⛔ evicts NOTHING, refuses LOUDLY

        const mrnv::TargetRow before = book.rec[slot];        // the ROW-SIZED edit buffer (64 B) — ⛔ no second book
        const uint16_t before_count = book.count;
        compose_(book.rec[slot], spec);
        book.count = static_cast<uint16_t>(target_population(book));
        // ⓘ `unchanged` is UNREACHABLE from here BY CONSTRUCTION and is said so rather than left to look like
        //   coverage: the chosen row was EMPTY (all 64 bytes zero) and the composed one carries `flags` set, so the
        //   row bytes always differ. `commit_` still asks, because that is what makes the guard the SERVICE's rather
        //   than each verb's.
        return commit_(book, static_cast<uint8_t>(slot), before, before_count);
    }

    // `admin-target set <sel> label=… hash=… layer=…` — ALL THREE mutable fields, every time. ⛔ The administration
    // key and the slot are PRESERVED: this verb re-keys nothing (that would silently move an operator's trust to a
    // different principal under a familiar label), and `layer=none` explicitly clears the path.
    TargetResult set(mrnv::TargetBlob& book, uint8_t slot, const TargetSpec& spec) {
        if (slot >= mrnv::kTargetSlots) return fail_(TargetErr::bad_args);
        if (_use.slot_in_use(slot))     return fail_(TargetErr::in_use);
        const TargetState s = load_for_write_(book);
        if (s == TargetState::io_failed) return fail_(TargetErr::store_io_failed);
        if (s == TargetState::invalid)   return fail_(TargetErr::store_invalid);
        if (!target_row_occupied(book.rec[slot])) return fail_(TargetErr::not_found);

        // The label must stay unique among the OTHER occupied rows.
        for (uint8_t i = 0; i < mrnv::kTargetSlots; ++i) {
            if (i == slot || !target_row_occupied(book.rec[i])) continue;
            const mrnv::TargetRow& r = book.rec[i];
            if (r.label_len == spec.label_len && !memcmp(r.label, spec.label, spec.label_len))
                return fail_(TargetErr::duplicate);
        }
        const mrnv::TargetRow before = book.rec[slot];
        const uint16_t before_count = book.count;
        TargetSpec keyed = spec;
        memcpy(keyed.admin_pub, before.admin_pub, sizeof keyed.admin_pub);   // ⛔ THE KEY IS IMMUTABLE
        if (!spec_valid_(keyed)) return fail_(TargetErr::bad_args);          // ⛔ nothing composed yet — zero writes
        compose_(book.rec[slot], keyed);
        book.count = before_count;                                            // population unchanged
        return commit_(book, slot, before, before_count);
    }

    // `admin-target remove <sel> confirm` — free one row, leaving a HOLE. ⛔ Never compacted (§6.5's rule, applied
    // to the controller's own book so a slot number stays a stable operator handle).
    TargetResult remove(mrnv::TargetBlob& book, uint8_t slot) {
        if (slot >= mrnv::kTargetSlots) return fail_(TargetErr::bad_args);
        if (_use.slot_in_use(slot))     return fail_(TargetErr::in_use);
        const TargetState s = load_for_write_(book);
        if (s == TargetState::io_failed) return fail_(TargetErr::store_io_failed);
        if (s == TargetState::invalid)   return fail_(TargetErr::store_invalid);
        if (!target_row_occupied(book.rec[slot])) return fail_(TargetErr::not_found);

        const mrnv::TargetRow before = book.rec[slot];
        const uint16_t before_count = book.count;
        book.rec[slot] = mrnv::TargetRow{};                   // ⛔ ZEROED IN PLACE — all 64 bytes
        book.count = static_cast<uint16_t>(before_count - 1);
        return commit_(book, slot, before, before_count);   // ⛔ `unchanged` unreachable: an occupied row became zero
    }

    // ★★ `admin-target reset confirm` — the ONLY write permitted over an INVALID record. ⛔ `io_failed` refuses
    //    even here; ⛔ `absent`/`ok` are `not_invalid`. ⛔ It is NOT a bulk-delete escape from `remove`.
    TargetResult recover(mrnv::TargetBlob& book) {
        if (_use.any_in_use()) return fail_(TargetErr::in_use);
        const TargetState s = target_state_of(_store.load(book), book);
        if (s == TargetState::io_failed) { mrnv::target_blob_init(book); return fail_(TargetErr::store_io_failed); }
        if (s != TargetState::invalid)   { mrnv::target_blob_init(book); return fail_(TargetErr::not_invalid); }
        mrnv::target_blob_init(book);                          // the candidate IS the empty book — one buffer
        if (!_store.save(book)) return fail_(TargetErr::nv_save_failed);
        TargetResult r;
        r.ok = true;
        return r;
    }

  private:
    static TargetResult fail_(TargetErr e) { TargetResult r; r.err = e; return r; }

    // ⛔ EVERY MUTATION RELOADS. The scratch is IO/candidate storage, ⛔ not a cache, so nothing may be carried
    //    over from a previous call — including a candidate a previous call failed to save.
    TargetState load_for_write_(mrnv::TargetBlob& book) {
        const TargetState s = target_state_of(_store.load(book), book);
        if (s == TargetState::absent) { mrnv::target_blob_init(book); return TargetState::ok; }
        if (s != TargetState::ok)     { mrnv::target_blob_init(book); return s; }
        return s;
    }

    // The spec's OWN rules, checked BEFORE anything is composed (parse -> validate -> construct).
    static bool spec_valid_(const TargetSpec& s) {
        if (admin_buf_all_zero(s.admin_pub, sizeof s.admin_pub)) return false;
        if (s.key_hash32 == 0)                                   return false;
        if (!target_label_valid(s.label, s.label_len))           return false;
        for (size_t i = s.label_len; i < sizeof s.label; ++i) if (s.label[i] != '\0') return false;
        if (s.hop_count > mrnv::kTargetHopMax)                   return false;
        for (uint8_t i = 0; i < s.hop_count; ++i)            if (s.hops[i] == 0) return false;
        for (size_t i = s.hop_count; i < sizeof s.hops; ++i) if (s.hops[i] != 0) return false;
        return true;
    }
    // ⛔ THE ROW IS ZEROED WHOLE FIRST. `reserved`, the label tail and the hop tail must be DETERMINISTIC or the
    //    byte-identical write guard would fire on stale bytes and rewrite flash for nothing.
    static void compose_(mrnv::TargetRow& row, const TargetSpec& s) {
        row = mrnv::TargetRow{};
        memcpy(row.admin_pub, s.admin_pub, sizeof row.admin_pub);
        row.key_hash32 = s.key_hash32;
        for (uint8_t i = 0; i < s.hop_count; ++i) row.hops[i] = s.hops[i];
        row.hop_count = s.hop_count;
        row.label_len = s.label_len;
        for (uint8_t i = 0; i < s.label_len; ++i) row.label[i] = s.label[i];
        row.flags = mrnv::kTargetFlagOccupied;
    }

    // ★★★ THE ONE COMMIT PATH (U2): whole-book validation -> byte comparison -> AT MOST ONE save, with the ROW and
    //     the COUNT restored on every failing arm so an unsuccessful candidate can never be reused.
    //   · The validation is what makes "a partially valid book is never adopted" true of the bytes this service
    //     COMPOSES, not only of the bytes it READS.
    //   · ⓘ AND IT IS REDUNDANT BY CONSTRUCTION FOR THIS SERVICE'S OWN CANDIDATES, said so rather than left to look
    //     like coverage (`firmware_admin_acl.h`'s `commit_` makes the identical statement): every mutation above
    //     starts from a book that already passed `target_content_valid` and changes it in exactly one permitted
    //     way. ⇒ no reachable path reaches this line with an invalid candidate, and ⛔ NO MUTATION OF IT CAN
    //     REDDEN THE SUITE — measured, not assumed: the entry written for it survived, and was WITHDRAWN from
    //     `--target=radmin4targets` rather than left as a permanent survivor. It is kept because it is the belt a
    //     future caller — Slice 8a's, composing a row itself — will need.
    //   · The comparison is a ROW-plus-COUNT compare rather than a second whole-book copy, deliberately: every
    //     mutation here touches exactly ONE row and the count, so the two are equivalent — and a second 2056-byte
    //     buffer is precisely what the residency decision forbids.
    TargetResult commit_(mrnv::TargetBlob& book, uint8_t slot, const mrnv::TargetRow& before,
                         uint16_t before_count) {
        if (!target_content_valid(book)) {
            book.rec[slot] = before;
            book.count = before_count;
            return fail_(TargetErr::store_invalid);
        }
        // ⛔ ZERO WRITES, and it is reported as a REFUSAL rather than a success: nothing became durable, so calling
        //    it `updated` would tell the operator a write happened that did not (brief §4.3). ⓘ Only `set` can
        //    reach it — `add` and `remove` always move the row bytes.
        if ((book.count == before_count) && !memcmp(&book.rec[slot], &before, sizeof before))
            return fail_(TargetErr::unchanged);
        if (!_store.save(book)) {
            book.rec[slot] = before;
            book.count = before_count;
            return fail_(TargetErr::nv_save_failed);
        }
        TargetResult r;
        r.ok = true;
        r.slot = slot;
        memcpy(r.admin_pub, book.rec[slot].admin_pub, sizeof r.admin_pub);
        return r;
    }

    ITargetStore&     _store;
    const ITargetUse& _use;
};

}  // namespace mrfw
