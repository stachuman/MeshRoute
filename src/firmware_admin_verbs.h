// MeshRoute — src/firmware_admin_verbs.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ REMOTE-ADMIN v2 SLICE 3 — the LOCAL USB VERB FAMILY for the two target stores: `admin-id …` and `acl …`.
//
// Authority: design §§6.4-6.6 and R-RA-21 (`physical` = first-owner / recovery / root-identity operations are
// LOCAL USB-ONLY in v2), R-RA-29 (the frozen fingerprint AND the whole-family BLE refusal). R-RA-29, verbatim:
//
//     "the whole target-side family — the ACL verbs (list/add/set/remove/recovery) and the administration-identity
//      verbs (show/generate/rotate) — is refused over BLE in `ble_dispatch_line` BEFORE the transport-neutral seam,
//      with one named `console_only` envelope"
//
// ★★ WHERE THE TRANSPORT DECISION LIVES, AND WHERE IT MUST NOT. This file is TRANSPORT-BLIND: it composes bytes
//    and hands them to a sink it cannot inspect, exactly as `firmware_ui_preset_verbs.h` does. There is no
//    `if (ble)` to write here, because there is nothing here that could ask. The ONE refusal lives in
//    `ble_dispatch_line` BEFORE `exec_console_line` — ⛔ never inside `dispatch`'s shared execution seam, whose
//    contract says in as many words that it "OWNS NO COMMAND-NAME SPECIAL CASE" and that transport-specific named
//    refusals "stay in their transports". `admin_verb_owns()` below is what the BLE guard's executed rows compare
//    the guard's own condition against, so the guard and the router can never drift apart silently.
//
// ★★★ WHY A PURE HEADER: `test_build_src = no` — `test/test_firmware_admin_verbs.cpp` pins EVERY EMITTED BYTE,
//     and `--target=radmin3verbs` can attack every token boundary, every reason mapping and every output line.
// ⛔ NO CAPABILITY MACRO IN THIS FILE ([[B255]] idiom).
//
// ⓘ WHAT THIS FILE DELIBERATELY DOES **NOT** HAVE:
//   · ⛔ NO `Print` AND NO `<Arduino.h>`: a `Print` would make this header un-host-compilable, which is the one
//     property that makes every byte below attackable (§B115). The device binding adapts the supplied `Print`.
//   · ⛔ NO GLOBAL SINK and no unsolicited output: every line goes to the sink the CALLER handed in.
//   · ⛔ NO SEED OR EXPANDED-SECRET BYTE, on any path, in any line — including the refusals and the boot report.
//   · ⛔ NO PARSER-CORE COMMAND ENUM. `lib/console/console_parse.cpp` owns the wire-command grammar; this family
//     is a ROUTER verb like `ui …` / `joinprofile …`, and adding it to the parser would put one command in two
//     authority tables (the ownership census exists to catch exactly that).
//   · ⛔ NO `help <topic>` SECTION. §0g retired those; the bare index gains the two names and the detail lands in
//     `docs/manual/command-reference.md` on QA PASS — the Author's landing, not the coder's.
//   · ⛔ NO CONTROLLER SIDE. `admin-key show self`, the target book and the OTHER node's USB output are Slice 4's
//     (bench Part 55b). This is the TARGET half of the exchange and claims nothing about the other end.
#pragma once
#include "firmware_command_context.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>                    // snprintf — the one composition primitive, always length-checked
#include <string.h>                   // memcmp/memcpy

#include "firmware_admin_acl.h"       // mrfw::AclService + the role/verdict vocabulary
#include "firmware_admin_identity.h"  // mrfw::AdminIdService + the fingerprint + the hex renderers
#include "firmware_config_parse.h"    // ★ mrfw::parse_hex32 / parse_index_strict / parse_confirm_token — REUSED (U1)

namespace mrfw {

// ---- the sink ------------------------------------------------------------------------------------------------
// ★ ONE LF-TERMINATED LINE PER CALL, and `n` COUNTS THE '\n'. Deliberately not a `Print`: see the head note.
//   The `IPresetLines` shape (U3) — one virtual, no state, no formatting knowledge.
struct IAdminLines {
    virtual ~IAdminLines() = default;
    virtual void line(const char* s, size_t n) = 0;
};

// ★★ THE LINE BUFFER, SIZED FROM THE WIDEST RECORD THIS FILE CAN PRODUCE rather than guessed. That record is
//    `acl added`:
//      "> acl added slot=" 17 + "9" 1 + " role=" 6 + "operator" 8 + " fp=" 4 + 16 + " pub=" 5 + 64 + "\n" 1 = 122
//    and the widest listing row is the same line without " added" = 116 B. ⛔ NO ESCAPE GROWTH IS POSSIBLE: every
//    variable field is fixed-width lowercase hex or one of two literal role tokens, and the slot/count fields are
//    bounded by `kAclSlots`.
inline constexpr size_t kAdminLineMax = 160;
static_assert(kAdminLineMax >= 17 + 1 + 6 + 8 + 4 + kAdminFpHex + 5 + kAdminKeyHex + 1 + 1,
              "firmware_admin_verbs.h: the widest `acl added` line no longer fits kAdminLineMax");
// ★★★ THE MAXIMUM WHOLE RESPONSE, DECLARED BEFORE EXECUTION (the brief's §6 item 2 requirement): the only
//     multi-line response is `acl list`, and it is bounded by TEN rows plus one end line:
//       10 x 116 + 42 = 1202 B — comfortably inside the 2048-byte console stage, so ⛔ ZERO `CONSOLE_DROP`.
inline constexpr size_t kAdminListMaxBytes = 10 * 116 + 42;
static_assert(kAdminListMaxBytes < 2048, "firmware_admin_verbs.h: `acl list` no longer fits the console stage");

// ---- bounded token scanning ----------------------------------------------------------------------------------
// ⛔ NOTHING BELOW ASSUMES A NUL. `dispatch()` hands a (pointer, length) span and the console stage is a byte
//    buffer, not a C string — a `strlen`/`atol` reading past `len` is precisely the class of defect
//    `parse_index_strict` exists to have removed one record over ([[B212]]'s neighbour).
inline bool admin_is_ws(char c) { return c == ' ' || c == '\t'; }
inline size_t admin_skip_ws(const char* s, size_t n, size_t i) {
    while (i < n && admin_is_ws(s[i])) ++i;
    return i;
}
// -> false when only whitespace remains from `i`.
inline bool admin_next_token(const char* s, size_t n, size_t& i, const char*& tok, size_t& tlen) {
    i = admin_skip_ws(s, n, i);
    if (i >= n) return false;
    const size_t start = i;
    while (i < n && !admin_is_ws(s[i])) ++i;
    tok  = s + start;
    tlen = i - start;
    return true;
}
inline bool admin_tail_empty(const char* s, size_t n, size_t i) { return admin_skip_ws(s, n, i) >= n; }
// ★ THE NAME IS `preset_word_is`'s, DELIBERATELY (U3): `tools/gen_command_inventory.py` derives the command
//   authority table by reading the string literal out of calls to a PINNED set of comparison helpers, and a
//   helper it does not know is a subcommand arm the inventory cannot see. ⛔ Do not rename it to something the
//   generator's `CMP_FUNCS` does not list without extending that list in the same edit.
inline bool admin_word_is(const char* tok, size_t tlen, const char* lit) {
    const size_t l = strlen(lit);
    return tlen == l && !memcmp(tok, lit, l);
}
// A bounded copy into a NUL-terminated scratch, so the SHIPPED decoders (`parse_hex32`, `parse_index_strict`) can
// be reused unchanged (U1) instead of forking a span-taking twin. ⛔ Refuses rather than truncating: a truncated
// key token would be decoded as a DIFFERENT key.
inline bool admin_token_copy(const char* tok, size_t tlen, char* buf, size_t cap) {
    if (tlen + 1 > cap) return false;
    memcpy(buf, tok, tlen);
    buf[tlen] = '\0';
    return true;
}

// ---- the two PRIMARY tokens, with EXACT boundaries -----------------------------------------------------------
// ★ "space/tab or end", ⛔ never a prefix: `admin-identity`, `admin-key`, `aclx` and `acls` are NOT this family,
//   and must fall through to the router's ordinary unknown-verb answer. `admin-key` in particular is the
//   CONTROLLER's future verb (Slice 4) and matching it here would claim a surface this slice does not implement.
inline bool admin_primary_is(const char* line, size_t len, const char* name) {
    const size_t l = strlen(name);
    if (len < l || memcmp(line, name, l)) return false;
    return len == l || admin_is_ws(line[l]);
}
// ★★ THE PREDICATE THE BLE GUARD IS MEASURED AGAINST, and the ONE place the family's membership is spelled.
//    It covers BOTH complete families, MALFORMED SUBFORMS INCLUDED (`acl bogus`, `admin-id`, `acl add`), because a
//    malformed line of an owned family is still a line this node must not answer over BLE. ⛔ It must NOT be a
//    broad `admin`-prefix test — that would swallow `admin-key`, which the controller half owns (Slice 4).
// ⓘ The ROUTER evaluates the same `admin_primary_is` calls at its own gated arm (`admin_router_arm`,
//   `firmware_commands.cpp`), which is where the inventory records the two top-level surfaces; this predicate is
//   listed in the generator's NON_COMMAND for exactly that reason — it re-asks one semantic question, it does not
//   add a second arm. ⛔ There is no third spelling of the family anywhere.
inline bool admin_verb_owns(const char* line, size_t len) {
    return admin_primary_is(line, len, "admin-id") || admin_primary_is(line, len, "acl");
}

// ---- the reason lexemes — EXHAUSTIVE, `-Werror=switch`-fenced ------------------------------------------------
// ⛔ NO `default:` ANYWHERE BELOW. A new verdict that nobody named would then fail to compile rather than print a
//    silent empty reason — the three enum->string bugs [[meshroute-warnings-gate-blocking]] records were exactly
//    this shape, and a byte-identity gate is structurally blind to them.
inline const char* admin_id_err_name(AdminIdErr e) {
    switch (e) {
        case AdminIdErr::none:            return "none";
        case AdminIdErr::bad_args:        return "bad_args";
        case AdminIdErr::absent:          return "absent";
        case AdminIdErr::store_invalid:   return "store_invalid";
        case AdminIdErr::store_io_failed: return "store_io_failed";
        case AdminIdErr::already_present: return "already_present";
        case AdminIdErr::not_invalid:     return "not_invalid";
        case AdminIdErr::entropy_failed:  return "entropy_failed";
        case AdminIdErr::nv_save_failed:  return "nv_save_failed";
        // ★ §RADMIN SLICE 5: the LIVE preparation refused before any write. ⛔ A DISTINCT lexeme from
        //   `entropy_failed` (the SEED draw, one step earlier, about a different secret) and from
        //   `nv_save_failed` (about the medium) — collapsing any two would misreport what survived.
        case AdminIdErr::runtime_unavailable: return "runtime_unavailable";
    }
    return "none";
}
inline const char* acl_err_name(AclErr e) {
    switch (e) {
        case AclErr::none:                 return "none";
        case AclErr::bad_args:             return "bad_args";
        case AclErr::store_invalid:        return "store_invalid";
        case AclErr::store_io_failed:      return "store_io_failed";
        case AclErr::not_invalid:          return "not_invalid";
        case AclErr::identity_absent:      return "identity_absent";
        case AclErr::identity_invalid:     return "identity_invalid";
        case AclErr::identity_io_failed:   return "identity_io_failed";
        case AclErr::duplicate_key:        return "duplicate_key";
        case AclErr::zero_key:             return "zero_key";
        case AclErr::acl_full:             return "acl_full";
        case AclErr::slot_empty:           return "slot_empty";
        case AclErr::first_owner_required: return "first_owner_required";
        case AclErr::last_owner:           return "last_owner";
        case AclErr::self_slot:            return "self_slot";
        case AclErr::nv_save_failed:       return "nv_save_failed";
        case AclErr::runtime_unavailable:  return "runtime_unavailable";   // ★ §RADMIN SLICE 5, as above
    }
    return "none";
}
// ★ THE FOUR STORAGE STATES, named identically for both stores so an operator reads one vocabulary.
inline const char* admin_id_state_name(AdminIdState s) {
    switch (s) {
        case AdminIdState::ok:        return "ok";
        case AdminIdState::absent:    return "absent";
        case AdminIdState::invalid:   return "invalid";
        case AdminIdState::io_failed: return "io_failed";
    }
    return "invalid";
}
inline const char* acl_state_name(AclState s) {
    switch (s) {
        case AclState::ok:        return "ok";
        case AclState::absent:    return "absent";
        case AclState::invalid:   return "invalid";
        case AclState::io_failed: return "io_failed";
    }
    return "invalid";
}

// ---- the emitters --------------------------------------------------------------------------------------------
// ⓘ EVERY ONE goes through `admin_emit`, so there is ONE place a line is length-checked and ONE place the '\n' is
//   accounted for. A `snprintf` that would have truncated is a REFUSAL to emit, ⛔ never a silently short line.
inline void admin_emit(IAdminLines& out, const char* buf, int n) {
    if (n <= 0 || static_cast<size_t>(n) >= kAdminLineMax) return;   // ⛔ unreachable by construction (see the
    out.line(buf, static_cast<size_t>(n));                           //    static_asserts above); fail-closed anyway
}
inline void admin_id_emit_err(IAdminLines& out, AdminIdErr e) {
    char b[kAdminLineMax];
    admin_emit(out, b, snprintf(b, sizeof b, "> admin-id err %s\n", admin_id_err_name(e)));
}
inline void acl_emit_err(IAdminLines& out, AclErr e) {
    char b[kAdminLineMax];
    admin_emit(out, b, snprintf(b, sizeof b, "> acl err %s\n", acl_err_name(e)));
}
// `verb` is the past-tense outcome word: ok / generated / rotated / recovered. ⛔ The public key is printed in
// FULL beside the fingerprint because the physical exchange copies the whole key (R-RA-29); the SEED never is.
inline void admin_id_emit_ok(IAdminLines& out, const char* verb, const uint8_t ed_pub[32]) {
    char fp[kAdminFpHex + 1], pub[kAdminKeyHex + 1], b[kAdminLineMax];
    admin_fp_hex(ed_pub, fp);
    admin_key_hex(ed_pub, pub);
    admin_emit(out, b, snprintf(b, sizeof b, "> admin-id %s fp=%s pub=%s\n", verb, fp, pub));
}
inline void admin_id_emit_boot(IAdminLines& out, const AdminIdBoot& r) {
    char b[kAdminLineMax];
    admin_emit(out, b, snprintf(b, sizeof b, "> admin-id boot state=%s\n", admin_id_state_name(r.state)));
}
inline void acl_emit_boot(IAdminLines& out, const AclBoot& r) {
    char b[kAdminLineMax];
    // ⚠ For every NON-ok state the three counters are ZERO and they mean "NO ACCEPTED ROWS" — ⛔ never "the flash
    //   is empty". `state=` carries that distinction, which is why it is printed FIRST and never omitted.
    admin_emit(out, b, snprintf(b, sizeof b, "> acl boot state=%s count=%u owners=%u operators=%u\n",
                                acl_state_name(r.state), (unsigned)r.census.count,
                                (unsigned)r.census.owners, (unsigned)r.census.operators));
}
inline void acl_emit_row(IAdminLines& out, uint8_t slot, const mrnv::AclRow& row) {
    char fp[kAdminFpHex + 1], pub[kAdminKeyHex + 1], b[kAdminLineMax];
    admin_fp_hex(row.ed_pub, fp);
    admin_key_hex(row.ed_pub, pub);
    AclRole r = AclRole::empty;
    (void)acl_role_assignable(row.role, r);            // ⛔ only OCCUPIED rows reach here (acl_content_valid holds)
    admin_emit(out, b, snprintf(b, sizeof b, "> acl slot=%u role=%s fp=%s pub=%s\n",
                                (unsigned)slot, acl_role_name(r), fp, pub));
}
inline void acl_emit_end(IAdminLines& out, const AclCensus& c) {
    char b[kAdminLineMax];
    admin_emit(out, b, snprintf(b, sizeof b, "> acl end count=%u owners=%u operators=%u\n",
                                (unsigned)c.count, (unsigned)c.owners, (unsigned)c.operators));
}

// ---- `admin-id <show|generate|rotate confirm|reset confirm>` -------------------------------------------------
// `args` is the RAW tail after the primary token — ⛔ possibly not NUL-terminated, ⛔ possibly empty.
// ★ FOUR SUBCOMMANDS, ⛔ no aliases and ⛔ no implicit bare default: `admin-id` alone is `bad_args`, because
//   guessing `show` from a bare verb is how an operator comes to believe he typed something else. An UNKNOWN
//   subcommand is `bad_args` too — ⛔ never a fall-through to the router's unknown-verb answer, which would make
//   `admin-id bogus` look like a command this build does not have.
// ⛔ A MISSING, WRONG OR EXTRA CONFIRM TOKEN COSTS ZERO WRITES AND ZERO ENTROPY DRAWS: `parse_confirm_token` is
//    reached BEFORE the service is called at all, and it is given the REMAINING RAW TAIL so its refusal of
//    trailing spaces and junk is preserved exactly (⛔ the tail is never trimmed into an accepted one).
inline void admin_id_verb(AdminIdService& svc, const char* args, size_t n, IAdminLines& out) {
    size_t i = 0;
    const char* tok = nullptr;
    size_t tlen = 0;
    if (!admin_next_token(args, n, i, tok, tlen)) { admin_id_emit_err(out, AdminIdErr::bad_args); return; }

    if (admin_word_is(tok, tlen, "show")) {
        if (!admin_tail_empty(args, n, i)) { admin_id_emit_err(out, AdminIdErr::bad_args); return; }
        const AdminIdResult r = svc.show();
        if (r.ok) admin_id_emit_ok(out, "ok", r.ed_pub); else admin_id_emit_err(out, r.err);
        return;
    }
    if (admin_word_is(tok, tlen, "generate")) {
        if (!admin_tail_empty(args, n, i)) { admin_id_emit_err(out, AdminIdErr::bad_args); return; }
        const AdminIdResult r = svc.generate();
        if (r.ok) admin_id_emit_ok(out, "generated", r.ed_pub); else admin_id_emit_err(out, r.err);
        return;
    }
    if (admin_word_is(tok, tlen, "rotate")) {
        if (!parse_confirm_token(args + i, n - i)) { admin_id_emit_err(out, AdminIdErr::bad_args); return; }
        const AdminIdResult r = svc.rotate();
        if (r.ok) admin_id_emit_ok(out, "rotated", r.ed_pub); else admin_id_emit_err(out, r.err);
        return;
    }
    if (admin_word_is(tok, tlen, "reset")) {
        if (!parse_confirm_token(args + i, n - i)) { admin_id_emit_err(out, AdminIdErr::bad_args); return; }
        const AdminIdResult r = svc.recover();
        if (r.ok) admin_id_emit_ok(out, "recovered", r.ed_pub); else admin_id_emit_err(out, r.err);
        return;
    }
    admin_id_emit_err(out, AdminIdErr::bad_args);
}

// ---- `acl <list|add …|set …|remove … confirm|reset confirm>` -------------------------------------------------
// ★ The ROLE token is parsed EXACTLY and ⛔ never defaulted: `operator` or `owner`, nothing else, no prefixes and
//   no case folding. A defaulted role is a silently-granted authority.
inline bool acl_parse_role(const char* tok, size_t tlen, AclRole& out) {
    if (admin_word_is(tok, tlen, "operator")) { out = AclRole::operator_; return true; }
    if (admin_word_is(tok, tlen, "owner"))    { out = AclRole::owner;     return true; }
    return false;
}
// ★ THE SLOT: `parse_index_strict` first (the whole token must be decimal digits — ⛔ no sign, ⛔ no `2junk`
//   prefix parse, ⛔ no overflow wrap, which is the `atol` defect §UI-15 slice 2 removed one record over), THEN
//   the 0..9 domain, THEN the narrowing. ⛔ Never narrow before checking the range.
inline bool acl_parse_slot(const char* tok, size_t tlen, uint8_t& out) {
    char buf[12];
    if (!admin_token_copy(tok, tlen, buf, sizeof buf)) return false;
    long v = 0;
    if (!parse_index_strict(buf, v)) return false;
    if (v < 0 || v >= static_cast<long>(mrnv::kAclSlots)) return false;
    out = static_cast<uint8_t>(v);
    return true;
}

inline void acl_verb(AclService& acl, AdminIdService& id, const char* args, size_t n, IAdminLines& out) {
    const uint8_t actor_slot = active_command_context().acl_slot;
    const AclActor actor{actor_slot != 0xFF, actor_slot}; // B370: authenticated slot, never inferred from the command
    size_t i = 0;
    const char* tok = nullptr;
    size_t tlen = 0;
    if (!admin_next_token(args, n, i, tok, tlen)) { acl_emit_err(out, AclErr::bad_args); return; }

    // ---- `acl list` — PUBLIC material only, occupied rows in SLOT ORDER, holes silently skipped -------------
    // ⛔ The listing does NOT renumber and does NOT compact: a hole at slot 3 stays a hole and slot 7 keeps its
    //    number, because the number is a stable wire handle (§6.5).
    if (admin_word_is(tok, tlen, "list")) {
        if (!admin_tail_empty(args, n, i)) { acl_emit_err(out, AclErr::bad_args); return; }
        mrnv::AclBlob b{};
        const AclState s = acl.read(b);
        if (s == AclState::io_failed) { acl_emit_err(out, AclErr::store_io_failed); return; }
        if (s == AclState::invalid)   { acl_emit_err(out, AclErr::store_invalid);   return; }
        // ⓘ An ABSENT record lists as an EMPTY one — `read()` left a valid empty blob — and the `count=0` end line
        //   is the honest answer: there are no accepted rows. The BOOT line is where absent-versus-empty is
        //   reported, because that is the fact an operator needs at provisioning time.
        for (uint8_t k = 0; k < mrnv::kAclSlots; ++k)
            if (acl_row_occupied(b.rec[k])) acl_emit_row(out, k, b.rec[k]);
        acl_emit_end(out, acl_census(b));
        return;
    }

    // ---- `acl add <operator|owner> <hex64>` ------------------------------------------------------------------
    if (admin_word_is(tok, tlen, "add")) {
        AclRole role = AclRole::empty;
        if (!admin_next_token(args, n, i, tok, tlen) || !acl_parse_role(tok, tlen, role)) {
            acl_emit_err(out, AclErr::bad_args); return;
        }
        if (!admin_next_token(args, n, i, tok, tlen)) { acl_emit_err(out, AclErr::bad_args); return; }
        // ⛔ A BOUNDED COPY, then the SHIPPED decoder (U1). The buffer is one byte wider than a legal token so a
        //    65-character token still reaches `parse_hex32` and is refused by its own trailing-junk check; a
        //    longer one is refused by the copy. Both answer `bad_args`, and ⛔ neither half-writes a key.
        char kb[kAdminKeyHex + 2];
        uint8_t key[32];
        if (!admin_token_copy(tok, tlen, kb, sizeof kb) || !parse_hex32(kb, key)) {
            acl_emit_err(out, AclErr::bad_args); return;
        }
        if (!admin_tail_empty(args, n, i)) { acl_emit_err(out, AclErr::bad_args); return; }
        // ★ THE ADMINISTRATION IDENTITY IS READ SEPARATELY, and only on this path: `list`/`set`/`remove` must not
        //   pay a second NV read for a fact they do not use. Design §6.4 forbids reporting a first-owner success
        //   against a root that is missing, corrupt or unreadable.
        const AclResult r = acl.add(id.state(), role, key);
        if (!r.ok) { acl_emit_err(out, r.err); return; }
        char fp[kAdminFpHex + 1], pub[kAdminKeyHex + 1], b[kAdminLineMax];
        admin_fp_hex(key, fp);
        admin_key_hex(key, pub);
        admin_emit(out, b, snprintf(b, sizeof b, "> acl added slot=%u role=%s fp=%s pub=%s\n",
                                    (unsigned)r.slot, acl_role_name(r.role), fp, pub));
        return;
    }

    // ---- `acl set <slot> <operator|owner>` -------------------------------------------------------------------
    if (admin_word_is(tok, tlen, "set")) {
        uint8_t slot = 0;
        AclRole role = AclRole::empty;
        if (!admin_next_token(args, n, i, tok, tlen) || !acl_parse_slot(tok, tlen, slot)) {
            acl_emit_err(out, AclErr::bad_args); return;
        }
        if (!admin_next_token(args, n, i, tok, tlen) || !acl_parse_role(tok, tlen, role)) {
            acl_emit_err(out, AclErr::bad_args); return;
        }
        if (!admin_tail_empty(args, n, i)) { acl_emit_err(out, AclErr::bad_args); return; }
        // Local USB still supplies no actor. 7b-1 (not Slice 6, as this formerly said) supplies
        // the authenticated remote slot, reusing the service's self-slot protection unchanged.
        const AclResult r = acl.set(slot, role, actor);
        if (!r.ok) { acl_emit_err(out, r.err); return; }
        char b[kAdminLineMax];
        admin_emit(out, b, snprintf(b, sizeof b, "> acl %s slot=%u role=%s\n",
                                    r.changed ? "updated" : "unchanged",
                                    (unsigned)r.slot, acl_role_name(r.role)));
        return;
    }

    // ---- `acl remove <slot> confirm` -------------------------------------------------------------------------
    if (admin_word_is(tok, tlen, "remove")) {
        uint8_t slot = 0;
        if (!admin_next_token(args, n, i, tok, tlen) || !acl_parse_slot(tok, tlen, slot)) {
            acl_emit_err(out, AclErr::bad_args); return;
        }
        if (!parse_confirm_token(args + i, n - i)) { acl_emit_err(out, AclErr::bad_args); return; }
        const AclResult r = acl.remove(slot, actor);
        if (!r.ok) { acl_emit_err(out, r.err); return; }
        char b[kAdminLineMax];
        admin_emit(out, b, snprintf(b, sizeof b, "> acl removed slot=%u\n", (unsigned)r.slot));
        return;
    }

    // ---- `acl reset confirm` — the confirm-gated physical recovery of an INVALID record ----------------------
    if (admin_word_is(tok, tlen, "reset")) {
        if (!parse_confirm_token(args + i, n - i)) { acl_emit_err(out, AclErr::bad_args); return; }
        const AclResult r = acl.recover();
        if (!r.ok) { acl_emit_err(out, r.err); return; }
        char b[kAdminLineMax];
        // ⛔ THE COUNTERS ARE LITERAL ZEROES BY CONSTRUCTION: recovery reinitialises to an EMPTY record and grants
        //    nobody. Deriving them from a re-read would be a second flash read of a record we just wrote.
        admin_emit(out, b, snprintf(b, sizeof b, "> acl recovered count=0 owners=0 operators=0\n"));
        return;
    }

    acl_emit_err(out, AclErr::bad_args);
}

// ---- the READ-ONLY boot report -------------------------------------------------------------------------------
// ★ TWO LINES, NO WRITES, NO KEY BYTES, NO AUTO-GENERATION. It installs nothing: there is no live cache in this
//   slice for it to install into, and a boot that minted a root on a transient read failure would be exactly the
//   "invented active owner" design §6.4 forbids.
inline void admin_boot_report(AdminIdService& id, AclService& acl, IAdminLines& out) {
    admin_id_emit_boot(out, id.boot_report());
    acl_emit_boot(out, acl.boot_report());
}

}  // namespace mrfw
