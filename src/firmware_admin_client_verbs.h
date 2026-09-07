// MeshRoute — src/firmware_admin_client_verbs.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ REMOTE-ADMIN v2 SLICE 4 — the CONTROLLER's LOCAL VERB FAMILIES `admin-key …` and `admin-target …`,
//     plus R-RA-30's BLE split predicate and the CLIENT-only `regen` admission/warning.
//
// Authority: design §§6.2-6.4, R-RA-29 (the frozen fingerprint) and R-RA-30, verbatim:
//
//     "Agree - implement the design's split, add the one-off xiao_mobile measurement"
//
// …over design §6.2's split, verbatim:
//
//     "`list` and `show` expose only public keys/fingerprints and may be used through USB or secured BLE.
//      Generating, importing, exporting, or removing secret material is physical USB-serial only in the first
//      implementation."
//
// ★★ WHERE THE TRANSPORT DECISION LIVES, AND WHERE IT MUST NOT. This file is TRANSPORT-BLIND: it composes bytes
//    and hands them to a sink it cannot inspect, exactly as `firmware_admin_verbs.h` does. There is no `if (ble)`
//    to write here, because there is nothing here that could ask. What this file DOES own is the PREDICATE
//    `admin_client_ble_refuses()` — a pure function of (line, len) — and the ONE evaluation of it lives in
//    `ble_dispatch_line` BEFORE `exec_console_line`, ⛔ never inside the shared execution seam, whose contract says
//    in as many words that it "OWNS NO COMMAND-NAME SPECIAL CASE". `tools/probe_console_sink/ble_guard.py` EXTRACTS
//    that guard's condition from `fw_main.cpp` and RUNS it against this predicate, so the guard and the router can
//    never drift apart silently.
//
// ★★★ WHY A PURE HEADER: `test_build_src = no` — `test/test_firmware_admin_client_verbs.cpp` pins EVERY EMITTED
//     BYTE, and `--target=radmin4verbs` can attack every token boundary, every reason mapping and every line.
// ⛔ NO CAPABILITY MACRO IN THIS FILE ([[B255]] idiom).
//
// ⓘ WHAT THIS FILE DELIBERATELY DOES **NOT** HAVE:
//   · ⛔ NO `Print` AND NO `<Arduino.h>`: a `Print` would make this header un-host-compilable, which is the one
//     property that makes every byte below attackable (§B115). The device binding adapts the supplied `Print`.
//   · ⛔ NO GLOBAL SINK and no unsolicited output: every line goes to the sink the CALLER handed in.
//   · ⛔ NO SEED BYTE ON ANY PATH BUT ONE — `admin-key export`, which exists to print one and is USB-only by the
//     guard above. ⛔ No refusal, no listing, no boot line and no `regen` line ever carries seed material.
//   · ⛔ NO REMOTE ISSUER. There is no `remote …` verb here: the controller can hold keys and targets after this
//     slice and can send NOTHING. Slice 8a is the first producer.
//   · ⛔ NO `help <topic>` SECTION. §0g retired those; the bare index gains the two names and the detail lands in
//     `docs/manual/command-reference.md` on QA PASS — the Author's landing, not the coder's.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>                    // snprintf — the one composition primitive, always length-checked
#include <string.h>                   // memcmp/memcpy

#include "firmware_admin_keyring.h"   // mrfw::MgmtKeyService + its verdict vocabulary
#include "firmware_admin_targets.h"   // mrfw::TargetService + its verdict vocabulary
#include "firmware_admin_verbs.h"     // ★ mrfw::IAdminLines + the bounded token scanners + admin_primary_is — REUSED
#include "firmware_config_parse.h"    // ★ mrfw::parse_hex32 / parse_index_strict / parse_confirm_token — REUSED (U1)

namespace mrfw {

// ---- the line buffer, SIZED FROM THE WIDEST RECORD THIS FILE CAN PRODUCE rather than guessed ------------------
// ★★ That record is a maximal `admin-target` listing row:
//      "> admin-target slot=" 20 + "31" 2 + " label=" 7 + 16 + " fp=" 4 + 16 + " pub=" 5 + 64
//      + " hash=0x" 8 + 8 + " layer=" 7 + "255,255,255" 11 + "\n" 1 = 169 B.
//    ⛔ NO ESCAPE GROWTH IS POSSIBLE: the label alphabet is `[A-Za-z0-9_.-]` (no spaces, no quotes), every other
//    variable field is fixed-width hex or a bounded decimal, and the slot/page/count fields are bounded by
//    `kTargetSlots`.
// ⛔ `kAdminLineMax` (160) IS DELIBERATELY NOT REUSED: it is sized for Slice 3's widest TARGET-side record and a
//    169-byte row does not fit it. Widening that constant would change a shipped family's bound inside this slice
//    (C1), so this family carries its own — same shape, its own arithmetic.
inline constexpr size_t kAdminClientLineMax = 192;
static_assert(kAdminClientLineMax >= 20 + 2 + 7 + 16 + 4 + kAdminFpHex + 5 + kAdminKeyHex + 8 + 8 + 7 + 11 + 1 + 1,
              "firmware_admin_client_verbs.h: the widest `admin-target` row no longer fits kAdminClientLineMax");
// ★★★ THE MAXIMUM WHOLE RESPONSE, DECLARED BEFORE EXECUTION. The two multi-line responses are bounded pages:
//       admin-target list : EIGHT physical rows x 169 + the 35-byte end line = 1387 B
//       admin-key list    : self + TEN rows x 106 + the 25-byte end line     = 1191 B
//     Both sit inside the 2048-byte console stage, so ⛔ ZERO `CONSOLE_DROP` and ⛔ no sink is grown.
inline constexpr size_t kTargetPageRows      = 8;
inline constexpr size_t kTargetPages         = mrnv::kTargetSlots / kTargetPageRows;   // 32 / 8 = 4
inline constexpr size_t kTargetPageMaxBytes  = kTargetPageRows * 169 + 35;             // 1387
inline constexpr size_t kMgmtKeyListMaxBytes = 106 + 10 * 106 + 25;                    // 1191
static_assert(kTargetPageMaxBytes  < 2048, "firmware_admin_client_verbs.h: a full target page no longer fits the stage");
static_assert(kMgmtKeyListMaxBytes < 2048, "firmware_admin_client_verbs.h: `admin-key list` no longer fits the stage");
static_assert(kTargetPages == 4, "firmware_admin_client_verbs.h: the four fixed pages are the frozen contract");

// ---- emission ------------------------------------------------------------------------------------------------
// ⓘ ONE place a line is length-checked and ONE place the '\n' is accounted for — `admin_emit`'s rule with this
//   family's bound. A `snprintf` that would have truncated is a REFUSAL to emit, ⛔ never a silently short line.
inline void admin_client_emit(IAdminLines& out, const char* buf, int n) {
    if (n <= 0 || static_cast<size_t>(n) >= kAdminClientLineMax) return;   // ⛔ unreachable by construction
    out.line(buf, static_cast<size_t>(n));                                 //    (see the static_asserts); fail-closed
}

// ---- the reason lexemes — EXHAUSTIVE, `-Werror=switch`-fenced ------------------------------------------------
// ⛔ NO `default:` ANYWHERE BELOW. A new verdict that nobody named fails to compile rather than printing a silent
//    empty reason — the three enum->string bugs [[meshroute-warnings-gate-blocking]] records were exactly this
//    shape, and a byte-identity gate is structurally blind to them.
inline const char* mgmt_key_err_name(MgmtKeyErr e) {
    switch (e) {
        case MgmtKeyErr::none:            return "none";
        case MgmtKeyErr::bad_args:        return "bad_args";
        case MgmtKeyErr::needs_confirm:   return "needs_confirm";
        case MgmtKeyErr::store_invalid:   return "store_invalid";
        case MgmtKeyErr::store_io_failed: return "store_io_failed";
        case MgmtKeyErr::not_invalid:     return "not_invalid";
        case MgmtKeyErr::not_found:       return "not_found";
        case MgmtKeyErr::occupied:        return "occupied";
        case MgmtKeyErr::duplicate:       return "duplicate";
        case MgmtKeyErr::bad_material:    return "bad_material";
        case MgmtKeyErr::entropy_failed:  return "entropy_failed";
        case MgmtKeyErr::in_use:          return "in_use";
        case MgmtKeyErr::nv_save_failed:  return "nv_save_failed";
    }
    return "none";
}
inline const char* target_err_name(TargetErr e) {
    switch (e) {
        case TargetErr::none:            return "none";
        case TargetErr::bad_args:        return "bad_args";
        case TargetErr::needs_confirm:   return "needs_confirm";
        case TargetErr::store_invalid:   return "store_invalid";
        case TargetErr::store_io_failed: return "store_io_failed";
        case TargetErr::not_invalid:     return "not_invalid";
        case TargetErr::not_found:       return "not_found";
        case TargetErr::ambiguous:       return "ambiguous";
        case TargetErr::duplicate:       return "duplicate";
        case TargetErr::full:            return "full";
        case TargetErr::in_use:          return "in_use";
        case TargetErr::unchanged:       return "unchanged";
        case TargetErr::nv_save_failed:  return "nv_save_failed";
    }
    return "none";
}
// ★ THE FOUR STORAGE STATES, named identically for both stores (and identically to Slice 3's) so an operator reads
//   ONE vocabulary across both product roles.
inline const char* mgmt_key_state_name(MgmtKeyState s) {
    switch (s) {
        case MgmtKeyState::ok:        return "ok";
        case MgmtKeyState::absent:    return "absent";
        case MgmtKeyState::invalid:   return "invalid";
        case MgmtKeyState::io_failed: return "io_failed";
    }
    return "invalid";
}
inline const char* target_state_name(TargetState s) {
    switch (s) {
        case TargetState::ok:        return "ok";
        case TargetState::absent:    return "absent";
        case TargetState::invalid:   return "invalid";
        case TargetState::io_failed: return "io_failed";
    }
    return "invalid";
}

// ---- the two PRIMARY tokens, with EXACT boundaries -----------------------------------------------------------
// ★ `admin_primary_is` is REUSED verbatim (U1): "space/tab or end", ⛔ never a prefix. `admin-keys`, `admin-key2`,
//   `admin-targets` and `admin` are NOT this family and fall through to the router's ordinary unknown-verb answer.
// ⛔ IT MUST NOT BE A BROAD `admin` PREFIX TEST — that would swallow `admin-id` and `acl`, which the TARGET half
//    owns (Slice 3) and whose own guard carries a DIFFERENT envelope.
inline bool admin_client_verb_owns(const char* line, size_t len) {
    return admin_primary_is(line, len, "admin-key") || admin_primary_is(line, len, "admin-target");
}
// ★★★ R-RA-30's SPLIT, AS ONE PURE PREDICATE. The BLE guard refuses every owned form EXCEPT one whose FIRST
//     sub-verb token is exactly `list` or `show`. ⇒ bare, whitespace-only, unknown, every mutation and both resets
//     refuse; a public listing passes and then undergoes FULL command parsing, so it cannot smuggle a second
//     operation — a malformed `list`/`show` tail is refused by the VERB below, on the same transport, as
//     `bad_args`, which is itself public.
// ⛔ TOKEN BOUNDARIES, ⛔ never a prefix: `listx`, `shown` and `LIST` are not the allowed words.
inline bool admin_client_ble_public(const char* line, size_t len) {
    if (!admin_client_verb_owns(line, len)) return false;
    const size_t fam = admin_primary_is(line, len, "admin-key") ? 9 : 12;   // strlen("admin-key") / ("admin-target")
    size_t i = fam;
    const char* tok = nullptr;
    size_t tlen = 0;
    if (!admin_next_token(line, len, i, tok, tlen)) return false;           // bare form — refused
    return admin_word_is(tok, tlen, "list") || admin_word_is(tok, tlen, "show");
}
inline bool admin_client_ble_refuses(const char* line, size_t len) {
    return admin_client_verb_owns(line, len) && !admin_client_ble_public(line, len);
}

// ---- bounded argument scanning, on RAW SPANS -----------------------------------------------------------------
// ⛔ NOTHING BELOW ASSUMES A NUL: `dispatch()` hands a (pointer, length) span and the console stage is a byte
//    buffer, not a C string. The bounded copies below exist so the SHIPPED decoders (`parse_hex32`,
//    `parse_index_strict`) can be reused unchanged (U1) instead of forked into span-taking twins.
// ★ `key0`…`key9` EXACTLY: three literal bytes and ONE digit. ⛔ No `key10` (two digits), ⛔ no `key00`, ⛔ no
//   `KEY0`, ⛔ no bare `0`. The keyring's capacity is ten and the grammar says so directly, so an out-of-range
//   index is a GRAMMAR refusal here rather than a service one.
inline bool admin_client_key_slot(const char* tok, size_t tlen, uint8_t& out) {
    if (tlen != 4 || memcmp(tok, "key", 3)) return false;
    if (tok[3] < '0' || tok[3] > '9') return false;
    out = static_cast<uint8_t>(tok[3] - '0');
    return true;
}
// `key=value` on a raw span. -> false when the token is not exactly this key with a non-empty value.
inline bool admin_client_kv(const char* tok, size_t tlen, const char* key,
                            const char*& val, size_t& vlen) {
    const size_t kl = strlen(key);
    if (tlen < kl + 2 || memcmp(tok, key, kl) || tok[kl] != '=') return false;
    val  = tok + kl + 1;
    vlen = tlen - kl - 1;
    return true;
}
// `0x` + 1..8 hex digits, non-zero. ★ THE `0x` PREFIX IS REQUIRED, which is what kills the id-versus-hash
// ambiguity — the same rule and the same reason as `parse_grant_args`' target token and `parse_hex32_0x`.
// ⛔ NO `atol`/`strtoul`: the span is not NUL-terminated and a partial parse would accept `0x1zz` as 1.
inline bool admin_client_hash(const char* v, size_t n, uint32_t& out) {
    if (n < 3 || v[0] != '0' || (v[1] != 'x' && v[1] != 'X')) return false;
    const size_t digits = n - 2;
    if (digits < 1 || digits > 8) return false;
    uint32_t acc = 0;
    for (size_t i = 2; i < n; ++i) {
        const char c = v[i];
        uint8_t d;
        if      (c >= '0' && c <= '9') d = static_cast<uint8_t>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<uint8_t>(10 + c - 'a');
        else if (c >= 'A' && c <= 'F') d = static_cast<uint8_t>(10 + c - 'A');
        else return false;
        acc = (acc << 4) | d;
    }
    if (acc == 0) return false;                 // hash 0 = "unset" everywhere in this codebase
    out = acc;
    return true;
}
// `none` or 1..3 comma-separated decimal layer ids in 1..255. ⛔ No empty element, ⛔ no trailing comma, ⛔ no
// fourth id (the carrier prepends our own layer — `lib/core/command.h:43`), ⛔ no clamp.
inline bool admin_client_layers(const char* v, size_t n, uint8_t hops[4], uint8_t& hop_count) {
    hops[0] = hops[1] = hops[2] = hops[3] = 0;
    hop_count = 0;
    if (n == 4 && !memcmp(v, "none", 4)) return true;
    size_t i = 0;
    while (i < n) {
        if (hop_count >= mrnv::kTargetHopMax) return false;      // ⛔ a fourth destination REFUSES
        uint32_t acc = 0;
        size_t digits = 0;
        while (i < n && v[i] >= '0' && v[i] <= '9') {
            acc = acc * 10 + static_cast<uint32_t>(v[i] - '0');
            if (acc > 255) return false;                         // ⛔ refuse, never wrap
            ++i; ++digits;
        }
        if (digits == 0 || acc == 0) return false;               // empty element, or the reserved 0
        hops[hop_count++] = static_cast<uint8_t>(acc);
        if (i == n) break;
        if (v[i] != ',') return false;
        ++i;
        if (i == n) return false;                                // ⛔ trailing comma
    }
    return hop_count > 0;
}
// The 16 canonical LOWERCASE hex characters of a fingerprint. ⛔ Case-sensitive on input: fingerprints are printed
// lowercase everywhere, and accepting `AB…` here would make two spellings of one handle.
inline bool admin_client_fp(const char* v, size_t n) {
    if (n != kAdminFpHex) return false;
    for (size_t i = 0; i < n; ++i) {
        const char c = v[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

// ---- the emitters --------------------------------------------------------------------------------------------
inline void mgmt_key_emit_err(IAdminLines& out, MgmtKeyErr e) {
    char b[kAdminClientLineMax];
    admin_client_emit(out, b, snprintf(b, sizeof b, "> admin-key err %s\n", mgmt_key_err_name(e)));
}
inline void target_emit_err(IAdminLines& out, TargetErr e) {
    char b[kAdminClientLineMax];
    admin_client_emit(out, b, snprintf(b, sizeof b, "> admin-target err %s\n", target_err_name(e)));
}
// `who` is `self` or `key<N>`; `verb` is the past-tense outcome word on a mutation. ⛔ The public key is printed in
// FULL beside the fingerprint because the physical exchange copies the whole key (R-RA-29); the SEED never is.
inline void mgmt_key_emit_row(IAdminLines& out, const char* who, const uint8_t ed_pub[32]) {
    char fp[kAdminFpHex + 1], pub[kAdminKeyHex + 1], b[kAdminClientLineMax];
    admin_fp_hex(ed_pub, fp);
    admin_key_hex(ed_pub, pub);
    admin_client_emit(out, b, snprintf(b, sizeof b, "> admin-key %s fp=%s pub=%s\n", who, fp, pub));
}
inline void mgmt_key_emit_slot(IAdminLines& out, uint8_t slot, const uint8_t ed_pub[32]) {
    char who[8];
    snprintf(who, sizeof who, "key%u", (unsigned)slot);
    mgmt_key_emit_row(out, who, ed_pub);
}
inline void mgmt_key_emit_done(IAdminLines& out, const char* verb, uint8_t slot, const uint8_t ed_pub[32]) {
    char fp[kAdminFpHex + 1], pub[kAdminKeyHex + 1], b[kAdminClientLineMax];
    admin_fp_hex(ed_pub, fp);
    admin_key_hex(ed_pub, pub);
    admin_client_emit(out, b, snprintf(b, sizeof b, "> admin-key %s key%u fp=%s pub=%s\n",
                                       verb, (unsigned)slot, fp, pub));
}
inline void mgmt_key_emit_end(IAdminLines& out, uint8_t count) {
    char b[kAdminClientLineMax];
    admin_client_emit(out, b, snprintf(b, sizeof b, "> admin-key end count=%u\n", (unsigned)count));
}
inline void mgmt_key_emit_boot(IAdminLines& out, const MgmtKeyBoot& r) {
    char b[kAdminClientLineMax];
    // ⚠ For every NON-ok state the count is ZERO and it means "NO ACCEPTED KEYS" — ⛔ never "the flash is empty".
    //   `state=` carries that distinction, which is why it is printed FIRST and never omitted.
    admin_client_emit(out, b, snprintf(b, sizeof b, "> admin-key boot state=%s count=%u\n",
                                       mgmt_key_state_name(r.state), (unsigned)r.count));
}
inline void target_emit_boot(IAdminLines& out, const TargetBoot& r) {
    char b[kAdminClientLineMax];
    admin_client_emit(out, b, snprintf(b, sizeof b, "> admin-target boot state=%s count=%u\n",
                                       target_state_name(r.state), (unsigned)r.count));
}
// The listing row — the SAME row `show` prints, composed once (U2).
// ⓘ `hash=` is UPPERCASE 8-digit hex, matching `print_identity`'s `key_hash32= 0x%08lX` spelling exactly, so one
//   operator reads one rendering of that field everywhere. The fingerprint and the key stay canonical LOWERCASE.
inline void target_emit_row(IAdminLines& out, uint8_t slot, const mrnv::TargetRow& r) {
    char fp[kAdminFpHex + 1], pub[kAdminKeyHex + 1], lab[sizeof r.label + 1], layer[16], b[kAdminClientLineMax];
    admin_fp_hex(r.admin_pub, fp);
    admin_key_hex(r.admin_pub, pub);
    size_t n = r.label_len <= sizeof r.label ? r.label_len : sizeof r.label;
    memcpy(lab, r.label, n);
    lab[n] = '\0';
    if (r.hop_count == 0) {
        memcpy(layer, "none", 5);
    } else {
        // ⛔ ONE literal format string, ⛔ never a ternary between two: a non-literal format is the shape
        //    `-Wformat-*` cannot check, and this line renders operator-visible routing.
        int at = 0;
        for (uint8_t i = 0; i < r.hop_count && i < mrnv::kTargetHopMax; ++i) {
            if (i) layer[at++] = ',';
            at += snprintf(layer + at, sizeof layer - (size_t)at, "%u", (unsigned)r.hops[i]);
        }
    }
    admin_client_emit(out, b, snprintf(b, sizeof b,
                                       "> admin-target slot=%u label=%s fp=%s pub=%s hash=0x%08lX layer=%s\n",
                                       (unsigned)slot, lab, fp, pub, (unsigned long)r.key_hash32, layer));
}
inline void target_emit_end(IAdminLines& out, uint8_t page, uint8_t count) {
    char b[kAdminClientLineMax];
    admin_client_emit(out, b, snprintf(b, sizeof b, "> admin-target end page=%u count=%u\n",
                                       (unsigned)page, (unsigned)count));
}
inline void target_emit_done(IAdminLines& out, const char* verb, uint8_t slot, const uint8_t admin_pub[32]) {
    char fp[kAdminFpHex + 1], pub[kAdminKeyHex + 1], b[kAdminClientLineMax];
    admin_fp_hex(admin_pub, fp);
    admin_key_hex(admin_pub, pub);
    admin_client_emit(out, b, snprintf(b, sizeof b, "> admin-target %s slot=%u fp=%s pub=%s\n",
                                       verb, (unsigned)slot, fp, pub));
}

// ---- `admin-key <list|show|generate|import|export|remove|reset>` ---------------------------------------------
// `args` is the RAW tail after the primary token — ⛔ possibly not NUL-terminated, ⛔ possibly empty.
// ★ SEVEN SUBCOMMANDS, ⛔ no aliases and ⛔ no implicit bare default: `admin-key` alone is `bad_args`, because
//   guessing `list` from a bare verb is how an operator comes to believe he typed something else. An UNKNOWN
//   subcommand is `bad_args` too — ⛔ never a fall-through to the router's unknown-verb answer.
// ⛔ A MISSING, WRONG OR EXTRA CONFIRM TOKEN COSTS ZERO WRITES AND ZERO ENTROPY DRAWS: `parse_confirm_token` is
//    reached BEFORE the service is called at all, and it is given the REMAINING RAW TAIL so its refusal of
//    trailing spaces and junk is preserved exactly (⛔ the tail is never trimmed into an accepted one).
inline void admin_key_verb(MgmtKeyService& svc, const char* args, size_t n, IAdminLines& out) {
    size_t i = 0;
    const char* tok = nullptr;
    size_t tlen = 0;
    if (!admin_next_token(args, n, i, tok, tlen)) { mgmt_key_emit_err(out, MgmtKeyErr::bad_args); return; }

    if (admin_word_is(tok, tlen, "list")) {
        if (!admin_tail_empty(args, n, i)) { mgmt_key_emit_err(out, MgmtKeyErr::bad_args); return; }
        // ★ SELF FIRST, THEN THE OCCUPIED SLOTS, THEN THE FOOTER. `self` is printed from the node's own identity
        //   and needs no store read, so it is emitted only when the keyring itself could be read — a listing that
        //   printed `self` and then refused would look like a keyring with one entry.
        const MgmtKeyList l = svc.list();
        if (!l.ok) { mgmt_key_emit_err(out, l.err); return; }
        const MgmtKeyResult me = svc.show_self();
        mgmt_key_emit_row(out, "self", me.ed_pub);
        for (uint8_t k = 0; k < mrnv::kMgmtKeySlots; ++k)
            if (l.rec[k].occupied) mgmt_key_emit_slot(out, k, l.rec[k].ed_pub);
        mgmt_key_emit_end(out, l.count);
        return;
    }
    if (admin_word_is(tok, tlen, "show")) {
        if (!admin_next_token(args, n, i, tok, tlen)) { mgmt_key_emit_err(out, MgmtKeyErr::bad_args); return; }
        if (admin_word_is(tok, tlen, "self")) {
            if (!admin_tail_empty(args, n, i)) { mgmt_key_emit_err(out, MgmtKeyErr::bad_args); return; }
            mgmt_key_emit_row(out, "self", svc.show_self().ed_pub);   // ⛔ ZERO store reads — see the service note
            return;
        }
        uint8_t slot = 0;
        if (!admin_client_key_slot(tok, tlen, slot) || !admin_tail_empty(args, n, i)) {
            mgmt_key_emit_err(out, MgmtKeyErr::bad_args); return;
        }
        const MgmtKeyResult r = svc.show(slot);
        if (!r.ok) { mgmt_key_emit_err(out, r.err); return; }
        mgmt_key_emit_slot(out, r.slot, r.ed_pub);
        return;
    }
    if (admin_word_is(tok, tlen, "generate")) {
        uint8_t slot = 0;
        if (!admin_next_token(args, n, i, tok, tlen) || !admin_client_key_slot(tok, tlen, slot) ||
            !admin_tail_empty(args, n, i)) { mgmt_key_emit_err(out, MgmtKeyErr::bad_args); return; }
        const MgmtKeyResult r = svc.generate(slot);
        if (!r.ok) { mgmt_key_emit_err(out, r.err); return; }
        mgmt_key_emit_done(out, "generated", r.slot, r.ed_pub);
        return;
    }
    if (admin_word_is(tok, tlen, "import")) {
        uint8_t slot = 0;
        if (!admin_next_token(args, n, i, tok, tlen) || !admin_client_key_slot(tok, tlen, slot)) {
            mgmt_key_emit_err(out, MgmtKeyErr::bad_args); return;
        }
        if (!admin_next_token(args, n, i, tok, tlen)) { mgmt_key_emit_err(out, MgmtKeyErr::bad_args); return; }
        // ⛔ A BOUNDED COPY, then the SHIPPED decoder (U1). The buffer is one byte wider than a legal token so a
        //    65-character token still reaches `parse_hex32` and is refused by its own trailing-junk check; a longer
        //    one is refused by the copy. Both answer `bad_args`, and ⛔ neither half-writes a seed.
        // ⚠⚠ THE SCRATCH BELOW HOLDS A SECRET IN BOTH SPELLINGS — the hex text AND the decoded bytes — so BOTH are
        //    guarded, and `parse_hex32`'s own 32-byte internal buffer is wiped by [[B321]]'s function-local guard.
        char sb[kAdminKeyHex + 2];
        uint8_t seed[32];
        SecretWipeGuard<char[kAdminKeyHex + 2]> gs{sb};
        SecretWipeGuard<uint8_t[32]>            gk{seed};
        if (!admin_token_copy(tok, tlen, sb, sizeof sb) || !parse_hex32(sb, seed) ||
            !admin_tail_empty(args, n, i)) { mgmt_key_emit_err(out, MgmtKeyErr::bad_args); return; }
        const MgmtKeyResult r = svc.import(slot, seed);
        if (!r.ok) { mgmt_key_emit_err(out, r.err); return; }
        mgmt_key_emit_done(out, "imported", r.slot, r.ed_pub);
        return;
    }
    if (admin_word_is(tok, tlen, "export")) {
        uint8_t slot = 0;
        if (!admin_next_token(args, n, i, tok, tlen) || !admin_client_key_slot(tok, tlen, slot) ||
            !admin_tail_empty(args, n, i)) { mgmt_key_emit_err(out, MgmtKeyErr::bad_args); return; }
        MgmtKeySeed s = svc.export_seed(slot);
        SecretWipeGuard<MgmtKeySeed> g{s};      // ⚠ carries THE SEED on every path out, refusals included
        if (!s.ok) { mgmt_key_emit_err(out, s.err); return; }
        char hex[kAdminKeyHex + 1], b[kAdminClientLineMax];
        SecretWipeGuard<char[kAdminKeyHex + 1]> gh{hex};
        SecretWipeGuard<char[kAdminClientLineMax]> gb{b};   // ⛔ the COMPOSED LINE holds the seed too
        admin_hex_lower(s.seed, 32, hex);
        admin_client_emit(out, b, snprintf(b, sizeof b, "> admin-key exported key%u seed=%s\n",
                                           (unsigned)s.slot, hex));
        return;
    }
    if (admin_word_is(tok, tlen, "remove")) {
        uint8_t slot = 0;
        if (!admin_next_token(args, n, i, tok, tlen) || !admin_client_key_slot(tok, tlen, slot)) {
            mgmt_key_emit_err(out, MgmtKeyErr::bad_args); return;
        }
        if (!parse_confirm_token(args + i, n - i)) { mgmt_key_emit_err(out, MgmtKeyErr::needs_confirm); return; }
        const MgmtKeyResult r = svc.remove(slot);
        if (!r.ok) { mgmt_key_emit_err(out, r.err); return; }
        char b[kAdminClientLineMax];
        admin_client_emit(out, b, snprintf(b, sizeof b, "> admin-key removed key%u\n", (unsigned)r.slot));
        return;
    }
    if (admin_word_is(tok, tlen, "reset")) {
        if (!parse_confirm_token(args + i, n - i)) { mgmt_key_emit_err(out, MgmtKeyErr::needs_confirm); return; }
        const MgmtKeyResult r = svc.recover();
        if (!r.ok) { mgmt_key_emit_err(out, r.err); return; }
        char b[kAdminClientLineMax];
        admin_client_emit(out, b, snprintf(b, sizeof b, "> admin-key reset\n"));
        return;
    }
    mgmt_key_emit_err(out, MgmtKeyErr::bad_args);
}

// ---- `admin-target <list|show|add|set|remove|reset>` ----------------------------------------------------------
// ★ THE SELECTOR IS ALWAYS PREFIXED — `label=<L>` or `fp=<16hex>` — and it resolves to ONE stored FULL KEY,
//   ⛔ never to a routing hash and ⛔ never to a row index: a bare number would make a display position look like a
//   trust handle. An unknown selector REFUSES `not_found`, an ambiguous one REFUSES `ambiguous`, and ⛔ neither
//   falls back to first-match or to the `/mrpeers` address book.
enum class TargetSel : uint8_t { bad, ok, none, many };
inline TargetSel target_resolve(const mrnv::TargetBlob& book, const char* tok, size_t tlen, uint8_t& slot) {
    const char* v = nullptr;
    size_t vn = 0;
    uint32_t mask = 0;
    if (admin_client_kv(tok, tlen, "label", v, vn)) {
        if (vn > sizeof(mrnv::TargetRow::label) || !target_label_valid(v, static_cast<uint8_t>(vn)))
            return TargetSel::bad;
        mask = target_mask_by_label(book, v, static_cast<uint8_t>(vn));
    } else if (admin_client_kv(tok, tlen, "fp", v, vn)) {
        if (!admin_client_fp(v, vn)) return TargetSel::bad;
        mask = target_mask_by_fp(book, v);
    } else {
        return TargetSel::bad;
    }
    switch (target_pick_from_mask(mask, slot)) {
        case TargetPick::none: return TargetSel::none;
        case TargetPick::one:  return TargetSel::ok;
        case TargetPick::many: return TargetSel::many;
    }
    return TargetSel::bad;
}
inline void target_emit_sel_err(IAdminLines& out, TargetSel s) {
    switch (s) {
        case TargetSel::none: target_emit_err(out, TargetErr::not_found); return;
        case TargetSel::many: target_emit_err(out, TargetErr::ambiguous); return;
        case TargetSel::bad:  target_emit_err(out, TargetErr::bad_args);  return;
        case TargetSel::ok:   break;
    }
}

// ⓘ THE SIGNATURE IS ON ONE LINE DELIBERATELY: `tools/gen_command_inventory.py`'s span scanner requires the
//   opening brace on the definition's own line, and a surface it cannot resolve is a HARD refusal of the whole
//   authority table (its refusal (a)). ⛔ Do not wrap it.
inline void admin_target_verb(TargetService& svc, mrnv::TargetBlob& book, const char* args, size_t n, IAdminLines& out) {
    size_t i = 0;
    const char* tok = nullptr;
    size_t tlen = 0;
    if (!admin_next_token(args, n, i, tok, tlen)) { target_emit_err(out, TargetErr::bad_args); return; }

    // ---- `admin-target list [page=0..3]` — FOUR FIXED PAGES OF EIGHT PHYSICAL SLOTS ------------------------
    // ⛔ THIS IS EXPLICIT PAGINATION, ⛔ NOT TRUNCATION: no page argument means page 0 BY CONTRACT, there is no
    //    automatic multi-page dump, indices are the PHYSICAL slot numbers and are stable, and the footer names the
    //    page beside the TOTAL occupied count so a reader can never mistake one page for the whole book.
    if (admin_word_is(tok, tlen, "list")) {
        uint8_t page = 0;
        if (admin_next_token(args, n, i, tok, tlen)) {
            const char* v = nullptr;
            size_t vn = 0;
            if (!admin_client_kv(tok, tlen, "page", v, vn) || vn != 1 || v[0] < '0' ||
                v[0] >= static_cast<char>('0' + kTargetPages) || !admin_tail_empty(args, n, i)) {
                target_emit_err(out, TargetErr::bad_args); return;
            }
            page = static_cast<uint8_t>(v[0] - '0');
        }
        const TargetState s = svc.read(book);
        if (s == TargetState::io_failed) { target_emit_err(out, TargetErr::store_io_failed); return; }
        if (s == TargetState::invalid)   { target_emit_err(out, TargetErr::store_invalid);   return; }
        // ⓘ An ABSENT record lists as an EMPTY one — `read()` left a valid empty book — and `count=0` is the honest
        //   answer: there are no accepted rows. The BOOT line is where absent-versus-empty is reported.
        const uint8_t lo = static_cast<uint8_t>(page * kTargetPageRows);
        for (uint8_t k = lo; k < lo + kTargetPageRows; ++k)
            if (target_row_occupied(book.rec[k])) target_emit_row(out, k, book.rec[k]);
        target_emit_end(out, page, target_population(book));
        return;
    }

    // ---- `admin-target show <label=…|fp=…>` — the SAME row `list` prints ------------------------------------
    if (admin_word_is(tok, tlen, "show")) {
        if (!admin_next_token(args, n, i, tok, tlen) || !admin_tail_empty(args, n, i)) {
            target_emit_err(out, TargetErr::bad_args); return;
        }
        const TargetState s = svc.read(book);
        if (s == TargetState::io_failed) { target_emit_err(out, TargetErr::store_io_failed); return; }
        if (s == TargetState::invalid)   { target_emit_err(out, TargetErr::store_invalid);   return; }
        uint8_t slot = 0;
        const TargetSel sel = target_resolve(book, tok, tlen, slot);
        if (sel != TargetSel::ok) { target_emit_sel_err(out, sel); return; }
        target_emit_row(out, slot, book.rec[slot]);
        return;
    }

    // ---- `admin-target add <LABEL> <hex64> hash=<0x…> [layer=<ids>]` ----------------------------------------
    if (admin_word_is(tok, tlen, "add")) {
        TargetSpec spec;
        if (!admin_next_token(args, n, i, tok, tlen) || tlen > sizeof spec.label ||
            !target_label_valid(tok, static_cast<uint8_t>(tlen))) {
            target_emit_err(out, TargetErr::bad_args); return;
        }
        memcpy(spec.label, tok, tlen);
        spec.label_len = static_cast<uint8_t>(tlen);
        if (!admin_next_token(args, n, i, tok, tlen)) { target_emit_err(out, TargetErr::bad_args); return; }
        char kb[kAdminKeyHex + 2];
        if (!admin_token_copy(tok, tlen, kb, sizeof kb) || !parse_hex32(kb, spec.admin_pub)) {
            target_emit_err(out, TargetErr::bad_args); return;
        }
        if (!admin_next_token(args, n, i, tok, tlen)) { target_emit_err(out, TargetErr::bad_args); return; }
        const char* v = nullptr;
        size_t vn = 0;
        if (!admin_client_kv(tok, tlen, "hash", v, vn) || !admin_client_hash(v, vn, spec.key_hash32)) {
            target_emit_err(out, TargetErr::bad_args); return;
        }
        // ★ AN OMITTED `layer=` EXPLICITLY MEANS SAME-LAYER (hop_count 0), and it is the ONLY optional token.
        if (admin_next_token(args, n, i, tok, tlen)) {
            if (!admin_client_kv(tok, tlen, "layer", v, vn) ||
                !admin_client_layers(v, vn, spec.hops, spec.hop_count) || !admin_tail_empty(args, n, i)) {
                target_emit_err(out, TargetErr::bad_args); return;
            }
        }
        const TargetResult r = svc.add(book, spec);
        if (!r.ok) { target_emit_err(out, r.err); return; }
        target_emit_done(out, "added", r.slot, r.admin_pub);
        return;
    }

    // ---- `admin-target set <sel> label=<L> hash=<0x…> layer=<none|ids>` -------------------------------------
    // ★ ALL THREE MUTABLE FIELDS ARE REQUIRED, EVERY TIME. ⛔ No partial update and ⛔ no hidden default: a `set`
    //   that silently kept an old path is how an operator comes to believe he cleared one. The KEY and the SLOT are
    //   preserved by the service — this verb cannot re-key a row.
    if (admin_word_is(tok, tlen, "set")) {
        const char* sel_tok = nullptr;
        size_t sel_len = 0;
        if (!admin_next_token(args, n, i, sel_tok, sel_len)) { target_emit_err(out, TargetErr::bad_args); return; }
        TargetSpec spec;
        const char* v = nullptr;
        size_t vn = 0;
        if (!admin_next_token(args, n, i, tok, tlen) || !admin_client_kv(tok, tlen, "label", v, vn) ||
            vn > sizeof spec.label || !target_label_valid(v, static_cast<uint8_t>(vn))) {
            target_emit_err(out, TargetErr::bad_args); return;
        }
        memcpy(spec.label, v, vn);
        spec.label_len = static_cast<uint8_t>(vn);
        if (!admin_next_token(args, n, i, tok, tlen) || !admin_client_kv(tok, tlen, "hash", v, vn) ||
            !admin_client_hash(v, vn, spec.key_hash32)) { target_emit_err(out, TargetErr::bad_args); return; }
        if (!admin_next_token(args, n, i, tok, tlen) || !admin_client_kv(tok, tlen, "layer", v, vn) ||
            !admin_client_layers(v, vn, spec.hops, spec.hop_count) || !admin_tail_empty(args, n, i)) {
            target_emit_err(out, TargetErr::bad_args); return;
        }
        const TargetState s = svc.read(book);
        if (s == TargetState::io_failed) { target_emit_err(out, TargetErr::store_io_failed); return; }
        if (s == TargetState::invalid)   { target_emit_err(out, TargetErr::store_invalid);   return; }
        uint8_t slot = 0;
        const TargetSel sel = target_resolve(book, sel_tok, sel_len, slot);
        if (sel != TargetSel::ok) { target_emit_sel_err(out, sel); return; }
        const TargetResult r = svc.set(book, slot, spec);      // ⛔ RELOADS — the resolution read is not trusted
        if (!r.ok) { target_emit_err(out, r.err); return; }
        target_emit_done(out, "updated", r.slot, r.admin_pub);
        return;
    }

    // ---- `admin-target remove <sel> confirm` ---------------------------------------------------------------
    if (admin_word_is(tok, tlen, "remove")) {
        const char* sel_tok = nullptr;
        size_t sel_len = 0;
        if (!admin_next_token(args, n, i, sel_tok, sel_len)) { target_emit_err(out, TargetErr::bad_args); return; }
        if (!parse_confirm_token(args + i, n - i)) { target_emit_err(out, TargetErr::needs_confirm); return; }
        const TargetState s = svc.read(book);
        if (s == TargetState::io_failed) { target_emit_err(out, TargetErr::store_io_failed); return; }
        if (s == TargetState::invalid)   { target_emit_err(out, TargetErr::store_invalid);   return; }
        uint8_t slot = 0;
        const TargetSel sel = target_resolve(book, sel_tok, sel_len, slot);
        if (sel != TargetSel::ok) { target_emit_sel_err(out, sel); return; }
        const TargetResult r = svc.remove(book, slot);
        if (!r.ok) { target_emit_err(out, r.err); return; }
        char b[kAdminClientLineMax];
        admin_client_emit(out, b, snprintf(b, sizeof b, "> admin-target removed slot=%u\n", (unsigned)r.slot));
        return;
    }

    // ---- `admin-target reset confirm` — the confirm-gated recovery of an INVALID record ---------------------
    if (admin_word_is(tok, tlen, "reset")) {
        if (!parse_confirm_token(args + i, n - i)) { target_emit_err(out, TargetErr::needs_confirm); return; }
        const TargetResult r = svc.recover(book);
        if (!r.ok) { target_emit_err(out, r.err); return; }
        char b[kAdminClientLineMax];
        admin_client_emit(out, b, snprintf(b, sizeof b, "> admin-target reset\n"));
        return;
    }

    target_emit_err(out, TargetErr::bad_args);
}

// ---- the READ-ONLY boot report --------------------------------------------------------------------------------
// ★ TWO LINES, NO WRITES, NO KEY OR SEED BYTES, NO AUTO-GENERATION. It installs nothing and creates nothing: a boot
//   that minted a management key on a transient read failure would be the "invented active owner" design §6.4
//   forbids, arrived at from the controller's side.
inline void admin_client_boot_report(MgmtKeyService& keys, TargetService& targets,
                                     mrnv::TargetBlob& book, IAdminLines& out) {
    mgmt_key_emit_boot(out, keys.boot_report());
    target_emit_boot(out, targets.boot_report(book));
}

// ---- `regen` on a CONTROLLER (design §6.4, brief §4.5) -------------------------------------------------------
// ★★★ THE ADMISSION PREDICATE, EVALUATED BEFORE ANY ENTROPY DRAW, NV WRITE OR IDENTITY CHANGE. `regen` rotates the
//     node's MESSAGING identity — which is the `self` key every target's ACL was granted against — so performing it
//     while a source-bound request, assembly, retained result or response ACK is outstanding would strand work that
//     can never be answered under the new key.
// ⛔ SLICE 4 HAS NO PRODUCER for that debt (Slice 8a is the first), so the PRODUCTION binding answers "no debt" and
//    every busy-predicate test is a **future-caller service test**, ⛔ never an executed live-RPC test.
struct IClientRemoteDebt {
    virtual ~IClientRemoteDebt() = default;
    virtual bool busy() const = 0;
};
inline bool client_regen_admitted(const IClientRemoteDebt& d) { return !d.busy(); }
// The EXACT refusal line, on the sink the caller was handed. ⛔ No second sink, ⛔ no `mrcon`.
inline void client_regen_emit_busy(IAdminLines& out) {
    char b[kAdminClientLineMax];
    admin_client_emit(out, b, snprintf(b, sizeof b, "> regen err remote_busy\n"));
}
// ★ THE WARNING, PRINTED ONLY AFTER A COMPLETE SUCCESSFUL REGENERATION, on the SAME sink. ⛔ Not on a refusal and
//   ⛔ not on a save failure — a warning about a rotation that did not happen is a false statement about state.
// ⓘ It says `preserved` about the two CONTROLLER stores because this slice PROVES it: `do_regen` writes `/mrid` and
//   nothing else, and neither store is loaded, drawn from or rewritten anywhere on that path.
inline void client_regen_emit_note(IAdminLines& out) {
    char b[kAdminClientLineMax];
    admin_client_emit(out, b, snprintf(b, sizeof b,
        "> regen note old self ACL grants do not follow the new key; dedicated keys and targets preserved\n"));
}

}  // namespace mrfw
