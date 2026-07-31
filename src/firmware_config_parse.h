// MeshRoute — src/firmware_config_parse.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Pure, device-free config/provisioning PARSE primitives extracted from fw_main.cpp (cleanup 2026-07-14,
// codebase-review triage step "extract pure parse/validate units first + add native tests"). Header-only +
// namespaced so both fw_main (device) and the native unit suite (test/test_firmware_config_parse.cpp) compile
// them — fw_main.cpp itself is outside the native build (test_build_src=no), so these had no test coverage.
// Behaviour-preserving: verbatim logic, only relocated. NO Arduino / Print / globals here — keep it pure.
#pragma once
#include <cstdint>
#include <cstdlib>                // atof/atol — phy_arg_take
#include <cstring>                // strcmp   — phy_arg_take (EXACT key match, §3-A.7)
#include "protocol_constants.h"   // §3-A.2: flood_hop_max (the hop_cap domain ceiling) — pure constexpr, no Arduino

namespace mrfw {

// §3-A.2 `cfg set` domain predicates (pure -> native-tested; handle_cfg_set consumes them).
// routing_sf/control_sf: the LoRa domain 5..12. The SF6 hardware FLOOR is deliberately NOT enforced (see the
// BENCH NOTE at the call site) — only the domain is.
// The parameter is `long` (not `int`) so the `sf=` PHY key, which is read with atol, can be checked WITHOUT a
// narrowing cast; int arguments promote losslessly, so no existing accept/reject decision moves.
inline bool valid_routing_sf(long v) { return v >= 5 && v <= 12; }
// leaf_id: 0..15 — the wire carries leaf_id ONLY as the cmd-byte low nibble (wire::flags_of = b & 0x0F) on every
// leaf-filtered frame, so >15 could never match ANY received frame (the node goes filter-deaf).
inline bool valid_leaf_id(int v) { return v >= 0 && v <= 15; }
// hop_cap (dv_hop_cap): 1..flood_hop_max(16) — it is the F RREQ TTL (codec: "config caps ttl <= 16") + the DV merge
// cap, and flood_hop_max clamps every flood horizon; 0 would kill ALL route learning.
inline bool valid_hop_cap(int v) { return v >= 1 && v <= static_cast<int>(MESHROUTE_NS::protocol::flood_hop_max); }

// Parse a spreading-factor list ("7,9,12" / "7 9 12" / any non-digit separators) into an SF bitmap
// (bit N = SF N). §3-A.7 FAIL-LOUD (unified on the parse_data_sfs grammar): ANY out-of-range number (not 5..12)
// rejects the WHOLE list -> 0 (was: silently ignored, so "7,13" silently became {7}). 0 = invalid; every caller
// must refuse on 0 (an empty sf_list blocks DATA sends entirely — [[data-sf-removed]]). `s` is read-only.
inline uint16_t parse_sf_list(const char* s) {
    uint16_t bm = 0; int v = 0; bool have = false;
    for (;; ++s) {
        const char ch = *s;
        if (ch >= '0' && ch <= '9') { v = v * 10 + (ch - '0'); have = true; }
        else {
            if (have) { if (v < 5 || v > 12) return 0; bm |= static_cast<uint16_t>(1u << v); }
            v = 0; have = false; if (!ch) break;
        }
    }
    return bm;
}

// Yield the next `key=value` token from *p (advancing p past it). A value may be "quoted" (so it can contain
// spaces — the leaf name). Returns false at end of string; on a malformed token (no `=`) *val is nullptr (the
// caller reports the bad key). key/val point into the caller's MUTABLE buffer, NUL-terminated. The shared grammar
// for the key=value provisioning verbs (create/join), mirroring `gateway`'s l0=/win0=/… named-param style.
inline bool kv_next(char*& p, char*& key, char*& val) {
    while (*p == ' ') ++p;
    if (!*p) return false;
    key = p;
    while (*p && *p != '=' && *p != ' ') ++p;                    // key up to '=' (or space/end = malformed)
    if (*p != '=') { if (*p == ' ') *p++ = '\0'; val = nullptr; return true; }
    *p++ = '\0';                                                 // terminate key, step past '='
    if (*p == '"') { ++p; val = p; while (*p && *p != '"') ++p; if (*p == '"') *p++ = '\0'; }   // quoted: spans spaces
    else           { val = p;      while (*p && *p != ' ') ++p; if (*p == ' ') *p++ = '\0'; }    // bare: up to next space
    return true;
}

// ---- The PHY triplet: `freq=<MHz> bw=<kHz> sf=<5..12>` (+ optional `layer=<1..255>`) ------------------------
// `join` / `create` / `team [new|<id>]` / `mobile register` all take the SAME operator-typed radio floor, and each
// re-spelled BOTH halves of it — the key match and the domain test. Stated once here (and reachable by the native
// suite, which matters: no scenario runs a console verb, so the corpus is structurally blind to all four).
//
// `bw` is FRACTIONAL kHz — 62.5 / 41.67 / 31.25 are real LoRa bandwidths, so it is atof, never atoi.
struct PhyArgs {
    double freq_mhz = 0.0;
    double bw_khz   = 0.0;    // callers where `bw=` is OPTIONAL pre-seed their own default (team/mobile: 125 kHz)
    long   sf       = 0;
    long   layer    = 0;      // the FULL 1..255 layer id; the wire leaf nibble is layer & 0x0F
    bool   has_freq = false, has_bw = false, has_sf = false, has_layer = false;
};

// Consume ONE kv_next token if it belongs to the triplet; false for anything else (including a valueless token, so
// the caller's unknown-key branch still fires exactly as before). `allow_layer` is false for the verbs that never
// accepted `layer=` — team / mobile register set the CURRENT layer's PHY, and silently swallowing the key there
// would be a behaviour change. All four keys are distinct, so where a caller places this in its else-if chain
// cannot change which branch wins.
inline bool phy_arg_take(PhyArgs& a, const char* key, const char* val, bool allow_layer) {
    if (!val) return false;
    if      (!strcmp(key, "freq")) { a.freq_mhz = atof(val); a.has_freq = true; }
    else if (!strcmp(key, "bw"))   { a.bw_khz   = atof(val); a.has_bw   = true; }
    else if (!strcmp(key, "sf"))   { a.sf       = atol(val); a.has_sf   = true; }
    else if (allow_layer && !strcmp(key, "layer")) { a.layer = atol(val); a.has_layer = true; }
    else return false;
    return true;
}

// The shared domain. ⚠ freq/bw are written as the NEGATION of the original reject-conditions rather than as a
// positive range test, and that is deliberate: `atof("nan")` yields NaN, for which BOTH `< lo` and `> hi` are
// false, so every one of these call sites has always ACCEPTED a NaN. `v >= lo && v <= hi` would start rejecting it
// — a behaviour change smuggled into a refactor (C1). Preserve it; close it in a fix slice if the owner wants it.
inline bool valid_freq_mhz(double mhz) { return !(mhz < 100.0 || mhz > 1000.0); }
inline bool valid_bw_khz(double khz)   { return !(khz < 7.0   || khz > 500.0);  }
// layer0_id: the full 1..255 layer id (0 = unset). Distinct from valid_leaf_id, which bounds the 0..15 wire nibble.
inline bool valid_layer0_id(long v)    { return v >= 1 && v <= 255; }

inline bool phy_args_in_range(const PhyArgs& a, bool with_layer) {
    return valid_freq_mhz(a.freq_mhz) && valid_bw_khz(a.bw_khz) && valid_routing_sf(a.sf)
        && (!with_layer || valid_layer0_id(a.layer));
}

// §team-ch-key (T-K1): parse EXACTLY 64 hex digits into 32 bytes — the `tkpub=`/`tkpriv=` provisioning params
// (and, later, the T-K4 QR fields). FAIL-LOUD (C2): false on a wrong length (63, 65, empty) or ANY non-hex
// character, and `out` is left untouched so a rejected token cannot half-write a key. Both cases are silent
// data-loss risks otherwise: a truncated hex string parsed leniently would install a DIFFERENT key than the
// operator pasted, and the node would then encrypt for a team that cannot read it.
// Case-insensitive (operators paste from either convention). Deliberately NOT accepting a `0x` prefix: these
// are fixed-width key blobs, not numbers, and `0x` + 64 digits is a length error worth reporting.
// The all-zero rejection lives one level down, in team_channel_key_derive — that is a crypto-domain rule
// (a dead RNG / a degenerate scalar), not a syntax one, and it must also cover the non-console callers.
inline bool parse_hex32(const char* s, uint8_t out[32]) {
    if (!s) return false;
    uint8_t buf[32];
    for (int i = 0; i < 32; ++i) {
        uint8_t byte = 0;
        for (int half = 0; half < 2; ++half) {
            const char c = s[2 * i + half];
            uint8_t nib;
            if      (c >= '0' && c <= '9') nib = static_cast<uint8_t>(c - '0');
            else if (c >= 'a' && c <= 'f') nib = static_cast<uint8_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') nib = static_cast<uint8_t>(c - 'A' + 10);
            else return false;                                  // non-hex OR a NUL from a SHORT string
            byte = static_cast<uint8_t>((byte << 4) | nib);
        }
        buf[i] = byte;
    }
    if (s[64] != '\0') return false;                            // trailing junk / too long
    for (int i = 0; i < 32; ++i) out[i] = buf[i];               // commit only after the whole token validated
    return true;
}

// §team-ch-key (T-K1): split the optional `tkpub=<hex64> tkpriv=<hex64>` pair OUT of a `team` tail, leaving every
// other token in `rest` for parse_phy_tail. The two families MUST be separated before that helper runs, because it
// refuses unknown keys — and teaching it about team keys is the wrong fix: it is ALSO `mobile register`'s parser
// (U1/U3), where `mobile register tkpub=…` must stay an error, exactly as `layer=` is.
//
// Lives HERE (pure, no Print/Arduino) rather than beside handle_team so the native suite can reach it: no scenario
// runs a console verb, so a helper left in firmware_config.cpp would have NO automated coverage at all.
// `scratch` is the caller's MUTABLE working copy (kv_next NUL-terminates in place); `rest` receives the surviving
// tokens re-emitted verbatim as `key=value` (or a bare key, so parse_phy_tail's unknown-key error still names it
// exactly as before). On any error `rest` is left unusable and `bad_key` names the offending key where meaningful.
// Duplicate keys are LAST-WINS, matching the rest of this grammar (`freq=1 freq=2` behaves the same way).
enum class TeamKeyTail : uint8_t {
    none,       // neither key present — the overwhelmingly common `team new` / `team <id>`
    ok,         // BOTH keys present and syntactically valid (crypto validation happens later, in Node::adopt)
    bad_hex,    // a tkpub=/tkpriv= value that is not EXACTLY 64 hex digits (bad_key names which)
    half_pair,  // only ONE of the two given — half a keypair is worse than none
    too_long    // the tail (or the filtered remainder) does not fit the caller's buffers
};
inline TeamKeyTail split_team_key_tail(const char* tail, char* scratch, size_t scratch_cap,
                                       char* rest, size_t rest_cap,
                                       uint8_t pub[32], uint8_t priv[32], const char*& bad_key) {
    bad_key = nullptr;
    size_t n = 0;
    for (const char* q = tail; *q; ++q) {
        if (n + 1 >= scratch_cap) return TeamKeyTail::too_long;
        scratch[n++] = *q;
    }
    scratch[n] = '\0';
    char* p = scratch; char* k; char* v;
    size_t rn = 0;
    bool have_pub = false, have_priv = false;
    if (!rest_cap) return TeamKeyTail::too_long;
    rest[0] = '\0';
    while (kv_next(p, k, v)) {
        const bool is_pub  = !strcmp(k, "tkpub");
        const bool is_priv = !strcmp(k, "tkpriv");
        if (is_pub || is_priv) {
            if (!v || !parse_hex32(v, is_pub ? pub : priv)) { bad_key = k; return TeamKeyTail::bad_hex; }
            if (is_pub) have_pub = true; else have_priv = true;
            continue;
        }
        const size_t need = strlen(k) + (v ? strlen(v) + 1 : 0) + (rn ? 1 : 0);   // [sep +] key [+ '=' + value]
        if (rn + need + 1 > rest_cap) return TeamKeyTail::too_long;               // +1 = the NUL
        if (rn) rest[rn++] = ' ';
        for (const char* q = k; *q; ++q) rest[rn++] = *q;
        if (v) { rest[rn++] = '='; for (const char* q = v; *q; ++q) rest[rn++] = *q; }
        rest[rn] = '\0';
    }
    if (!have_pub && !have_priv) return TeamKeyTail::none;
    if (have_pub != have_priv)   return TeamKeyTail::half_pair;
    return TeamKeyTail::ok;
}

// §team-ch-key (T-K3): parse `team grantkey`'s argument list. Grammar (kv/flag conventions per the 2026-07-03 console
// cleanup, target spelling per `reqpubkey`, lib/console/console_parse.cpp:166):
//
//     team grantkey <0xhash | team-id> [name="<text>"] [-t]
//
//   · `0x…`  1..8 hex digits -> the target's key_hash32. The 0x prefix is REQUIRED, which is what kills the
//     id-vs-hash ambiguity (the same rule and the same reason as parse_hex32_0x).
//   · a bare decimal 1..254  -> a teammate's team_local_id, as seen in the roster/beacons; the CALLER resolves it to a
//     hash via Node::team_key_of_id and reports `bad_target` if that teammate has not been heard. Implicitly TEAM plane.
//   · `name="…"`             -> the optional team label that rides the grant body (quoted, so it may contain spaces —
//     kv_next handles the quoting). Longer than 32 -> too_long (C2: refuse, never silently truncate an operator label).
//   · `-t`                   -> force the TEAM plane, exactly as on `send` / `reqpubkey`.
//
// ⚠ U1 NOTE, reported not hidden: the 0x-hex token grammar below is the SAME grammar as
// lib/console/console_parse.cpp:69 `parse_hex32_0x`, which is NOT reused because its parameter type (`Tok`) is private
// to that TU and exporting it is a pure refactor of a shipped header (C1). Two ~10-line token scanners now state the
// same rule; folding them together belongs in a cleanup slice, and this note is here so it can be found.
// Lives in this pure header (no Print/Arduino) for the same reason split_team_key_tail does: no scenario runs a console
// verb, so parsing left in firmware_config.cpp would have zero automated coverage.
enum class GrantArgs : uint8_t {
    ok,           // target_hash OR target_id set (exactly one), name/team_plane filled
    missing,      // no target token at all
    bad_target,   // the target token is neither `0x`+1..8 hex nor a decimal 1..254
    bad_key,      // an unknown key/flag in the tail (bad_key names it)
    too_long,     // the tail does not fit the scratch buffer, or name= exceeds 32 chars
};
struct GrantArgsOut {
    uint32_t target_hash = 0;    // set iff the token was 0x-hex
    uint8_t  target_id   = 0;    // set iff the token was a bare decimal 1..254 (a team_local_id)
    char     name[33]    = {};   // NUL-terminated; empty = no name given
    uint8_t  name_len    = 0;
    bool     team_plane  = false;   // `-t`, or implied by a bare team-id target
};
inline GrantArgs parse_grant_args(const char* tail, char* scratch, size_t scratch_cap,
                                  GrantArgsOut& out, const char*& bad_key) {
    bad_key = nullptr;
    out = GrantArgsOut{};
    while (*tail == ' ') ++tail;
    if (!*tail) return GrantArgs::missing;
    // ---- the target token (up to the first space) ----
    const char* t = tail;
    size_t tn = 0;
    while (t[tn] && t[tn] != ' ') ++tn;
    if (tn >= 3 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        const size_t digits = tn - 2;
        if (digits < 1 || digits > 8) return GrantArgs::bad_target;
        uint32_t v = 0;
        for (size_t i = 2; i < tn; ++i) {
            const char c = t[i];
            uint8_t d;
            if      (c >= '0' && c <= '9') d = static_cast<uint8_t>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<uint8_t>(10 + c - 'a');
            else if (c >= 'A' && c <= 'F') d = static_cast<uint8_t>(10 + c - 'A');
            else return GrantArgs::bad_target;
            v = (v << 4) | d;
        }
        if (v == 0) return GrantArgs::bad_target;               // hash 0 = "unset" everywhere in this codebase
        out.target_hash = v;
    } else {
        uint32_t v = 0;
        for (size_t i = 0; i < tn; ++i) {
            if (t[i] < '0' || t[i] > '9') return GrantArgs::bad_target;
            v = v * 10 + static_cast<uint32_t>(t[i] - '0');
            if (v > 254) return GrantArgs::bad_target;          // 255/0xFF is reserved; 0 is unset
        }
        if (v == 0) return GrantArgs::bad_target;
        out.target_id  = static_cast<uint8_t>(v);
        out.team_plane = true;                                  // a bare team id is implicitly a TEAM-plane target
    }
    // ---- the kv/flag tail ----
    const char* rest = t + tn;
    size_t n = 0;
    for (const char* q = rest; *q; ++q) {
        if (n + 1 >= scratch_cap) return GrantArgs::too_long;
        scratch[n++] = *q;
    }
    scratch[n] = '\0';
    char* p = scratch; char* k; char* v;
    while (kv_next(p, k, v)) {
        if (!strcmp(k, "-t") && !v) { out.team_plane = true; continue; }
        if (!strcmp(k, "name")) {
            if (!v) { bad_key = k; return GrantArgs::bad_key; }
            size_t ln = strlen(v);
            if (ln > 32) return GrantArgs::too_long;
            for (size_t i = 0; i < ln; ++i) out.name[i] = v[i];
            out.name[ln] = '\0'; out.name_len = static_cast<uint8_t>(ln);
            continue;
        }
        bad_key = k;
        return GrantArgs::bad_key;
    }
    return GrantArgs::ok;
}

// ★★ §team-target (BUG FIX 2026-07-30) — `team <garbage>` USED TO LEAVE THE TEAM.
// `handle_team` read its first token with `strtoul(args, &endp, 0)`. strtoul consumes ZERO characters from a
// non-numeric tail and returns 0 — MEASURED, not assumed: strtoul("exportky") == 0 with endp == the input,
// likewise "grantky", "nwe", "new". And `team 0` MEANS LEAVE. So EVERY near-spelling of a subcommand —
// `team exportky`, `team grantky`, `team nwe` — silently left the team, dropping the learned team plane and the
// team_local_id. `exportkey`/`grantkey`/`new` are matched before the numeric parse, so only the near-misses bit,
// and a MOBILE was masked (parse_phy_tail refuses the leftover unknown key first) — but a node with
// `is_mobile == 0` and a non-zero `team_id` LEFT, and that is reachable from the console the owner types at.
//
// ★★ §team-target-whole (BUG FIX 2026-07-31, register B1) — AND `team 88A672BA` JOINED TEAM 88.
// The 07-30 rule's clause (2) was gated on `v == 0`, which is the destructive outcome but not the only WRONG one.
// A hex team id typed WITHOUT the `0x` prefix — the way every operator writes a hash elsewhere — is read by
// strtoul base 0 as DECIMAL: `88A672BA` yields 88, endp stops at the `A`, and 88 != 0 so the zero-gate never
// looked. ⇒ you JOIN A DIFFERENT TEAM, with no error and no way to tell from the console. Same silent-wrong-state
// family as the leave bug, arriving through the accept side instead of the refuse side.
//
// THE RULE — now ONE clause, applied unconditionally:
//   (1) the token must BEGIN WITH A DIGIT to be read as a team id; anything else is the caller's LOUD refusal
//       (C2 — never a silent no-op, never a fall-through to leave). It also refuses two spellings that were
//       silently WRONG rather than merely mistyped: `team -1` (strtoul yields 0xFFFFFFFF, i.e. joining a garbage
//       team) and `team +5`, neither of which any usage line offers.
//   (2) ★ the digits strtoul consumed must span the WHOLE target token — a PARTIALLY-consumed token is refused
//       whatever it evaluated to. Covers the destructive zero spellings (`0x`, `08`, `0abc` — strtoul reads only
//       the leading "0", so a leading-digit rule ALONE still LEFT THE TEAM) *and* the mis-join ones
//       (`88A672BA` -> 88, `12abc` -> 12, `1x` -> 1). ⚠ Widening this from `v == 0` to every value is the whole
//       of fix B1; `team 12abc` used to be accepted as team 12 with `abc` handed to the tail parsers, and that
//       leniency is now gone deliberately — a suffix the id parser did not consume is a TYPO, not a tail.
// ★ THE LEGITIMATE PHY/KEY TAIL IS UNAFFECTED, and this is the property to keep true: the token is measured only
// up to the FIRST SPACE, so `team 0x88A672BA freq=868 sf=7` and `team 7 freq=869.0` still parse — the token is
// fully consumed and the tail starts at the space. Measured, and pinned by the native tests below.
// `team 0` (leave), `team 00`, `team 0x0`, `team 42`, `team 0x2a`, `team 010`, `team 12345`, `team 0xDEADBEEF`
// and `team <id> freq=…` all keep working VERBATIM: this gates entry only, strtoul with base 0 still converts.
//
// ✖ MISSING / NOT THIS SLICE (C1) — the OUT-OF-RANGE token, measured 2026-07-31 and left open on purpose. A
// fully-consumed token outside 32 bits still lands: `team 4294967296` truncates to 0 = LEAVE on a 64-bit
// `unsigned long` (native), while on the 32-bit boards strtoul saturates to ULONG_MAX+ERANGE so the SAME command
// joins garbage team 0xFFFFFFFF — a real sim-vs-metal divergence. It needs an `errno`/width check, which is a
// different clause (a range rule, not a syntax one) and would want its own before/after measurement.
//
// FAIL-CLOSED: on false, `out_id` and `out_tail` are left UNTOUCHED, so a refused token cannot half-write the id
// the caller is about to persist. That is the property the native test pins — it seeds out_id with a live
// team_id and asserts the garbage case leaves it SURVIVING, which is the assertion that protects the operator
// (an error string alone would not).
inline bool parse_team_target(const char* s, uint32_t& out_id, const char*& out_tail) {
    if (!s || s[0] < '0' || s[0] > '9') return false;   // (1) no leading digit => not a team id
    char* endp = nullptr;
    const uint32_t v = static_cast<uint32_t>(strtoul(s, &endp, 0));   // base 0: decimal, 0x-hex and leading-0 octal, exactly as before
    size_t tn = 0; while (s[tn] && s[tn] != ' ') ++tn;                // (2) the target token = up to the first space
    if (static_cast<size_t>(endp - s) != tn) return false;            // partially consumed: `0x`/`08` (would LEAVE), `88A672BA`/`12abc` (would MIS-JOIN)
    out_id = v; out_tail = endp;                        // committed only after the whole token was accepted
    return true;
}

// FNV-1a/32 over the 8 little-endian bytes of (a ‖ b). Used to MINT a fresh team_id = hash(our key ‖ HW-RNG nonce).
inline uint32_t team_fnv1a32(uint32_t a, uint32_t b) {
    uint32_t h = 2166136261u;
    const uint8_t by[8] = { (uint8_t)a, (uint8_t)(a>>8), (uint8_t)(a>>16), (uint8_t)(a>>24),
                            (uint8_t)b, (uint8_t)(b>>8), (uint8_t)(b>>16), (uint8_t)(b>>24) };
    for (int i = 0; i < 8; ++i) { h ^= by[i]; h *= 16777619u; }
    return h;
}

}  // namespace mrfw
