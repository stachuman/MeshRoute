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
#include "protocol_constants.h"   // §3-A.2: flood_hop_max (the hop_cap domain ceiling) — pure constexpr, no Arduino

namespace mrfw {

// §3-A.2 `cfg set` domain predicates (pure -> native-tested; handle_cfg_set consumes them).
// routing_sf/control_sf: the LoRa domain 5..12. The SF6 hardware FLOOR is deliberately NOT enforced (see the
// BENCH NOTE at the call site) — only the domain is.
inline bool valid_routing_sf(int v) { return v >= 5 && v <= 12; }
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

// FNV-1a/32 over the 8 little-endian bytes of (a ‖ b). Used to MINT a fresh team_id = hash(our key ‖ HW-RNG nonce).
inline uint32_t team_fnv1a32(uint32_t a, uint32_t b) {
    uint32_t h = 2166136261u;
    const uint8_t by[8] = { (uint8_t)a, (uint8_t)(a>>8), (uint8_t)(a>>16), (uint8_t)(a>>24),
                            (uint8_t)b, (uint8_t)(b>>8), (uint8_t)(b>>16), (uint8_t)(b>>24) };
    for (int i = 0; i < 8; ++i) { h ^= by[i]; h *= 16777619u; }
    return h;
}

}  // namespace mrfw
