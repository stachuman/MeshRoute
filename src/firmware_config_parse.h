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

// FNV-1a/32 over the 8 little-endian bytes of (a ‖ b). Used to MINT a fresh team_id = hash(our key ‖ HW-RNG nonce).
inline uint32_t team_fnv1a32(uint32_t a, uint32_t b) {
    uint32_t h = 2166136261u;
    const uint8_t by[8] = { (uint8_t)a, (uint8_t)(a>>8), (uint8_t)(a>>16), (uint8_t)(a>>24),
                            (uint8_t)b, (uint8_t)(b>>8), (uint8_t)(b>>16), (uint8_t)(b>>24) };
    for (int i = 0; i < 8; ++i) { h ^= by[i]; h *= 16777619u; }
    return h;
}

}  // namespace mrfw
