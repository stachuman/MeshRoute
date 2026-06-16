// MeshRoute — lib/console/console_parse.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#include "console_parse.h"
#include "frame_codec.h"   // DATA_FLAG_E2E_ACK_REQ (the canonical wire flag the RX acts on)
#include <cstring>

namespace meshroute::console {
namespace {

struct Scan { const char* p; const char* end; };
void skip_ws(Scan& s) { while (s.p < s.end && (*s.p == ' ' || *s.p == '\t')) ++s.p; }

struct Tok { const char* s; size_t n; };
// Reads a non-space token; returns {start,len}. len==0 at end-of-line.
Tok token(Scan& s) {
    skip_ws(s);
    const char* b = s.p;
    while (s.p < s.end && *s.p != ' ' && *s.p != '\t') ++s.p;
    return { b, static_cast<size_t>(s.p - b) };
}
bool tok_eq(const Tok& t, const char* lit) {
    return t.n == std::strlen(lit) && std::memcmp(t.s, lit, t.n) == 0;
}
// Parse a decimal token into [0,max]; false on empty/non-digit/overflow.
bool parse_u32_tok(const Tok& t, uint32_t max, uint32_t& out) {
    if (t.n == 0) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < t.n; ++i) {
        char c = t.s[i];
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<uint32_t>(c - '0');
        if (v > max) return false;
    }
    out = v;
    return true;
}

// Parse up to 8 hex digits into a u32; false on empty/non-hex/overflow (>8 digits).
bool parse_hex32_tok(const Tok& t, uint32_t& out) {
    if (t.n == 0 || t.n > 8) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < t.n; ++i) {
        char c = t.s[i]; uint32_t d;
        if (c >= '0' && c <= '9')      d = static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') d = 10u + static_cast<uint32_t>(c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10u + static_cast<uint32_t>(c - 'A');
        else return false;
        v = (v << 4) | d;
    }
    out = v;
    return true;
}

}  // namespace

ParseErr parse_command(const char* line, size_t len, Command& out) {
    Scan s{ line, line + len };
    Tok verb = token(s);
    if (verb.n == 0) return ParseErr::empty;

    //   resolve <hash> [hard] — diagnostic hash-locate (H flood); the answer arrives async via hash_resolved.
    if (tok_eq(verb, "resolve")) {
        Tok arg = token(s);
        uint32_t hash = 0;
        if (!parse_hex32_tok(arg, hash)) return ParseErr::bad_args;
        Tok opt = token(s);
        const bool hard = (opt.n != 0) && tok_eq(opt, "hard");
        if (opt.n != 0 && !hard) return ParseErr::bad_args;        // the only valid 2nd arg is `hard`
        out = Command{};
        out.kind = CmdKind::resolve;
        out.u.resolve.dst_hash = hash;
        out.u.resolve.hard     = hard;
        return ParseErr::ok;
    }

    //   reqpubkey <hash> — §6: user-triggered on-air pubkey request (a HARD WANT_PUBKEY H flood). The only auto-source.
    if (tok_eq(verb, "reqpubkey")) {
        Tok arg = token(s);
        uint32_t hash = 0;
        if (!parse_hex32_tok(arg, hash)) return ParseErr::bad_args;
        out = Command{};
        out.kind = CmdKind::reqpubkey;
        out.u.resolve.dst_hash = hash;
        return ParseErr::ok;
    }

    //   send_layer     <hash> <l1,l2,…> <text> — explicit-path cross-layer DM along the given destination layers
    //   send_layer_ack <hash> <l1,l2,…> <text> — same + request the end-to-end ack (the companion reuses this parser)
    {
        const bool is_layer     = tok_eq(verb, "send_layer");
        const bool is_layer_ack = tok_eq(verb, "send_layer_ack");
        if (is_layer || is_layer_ack) {
            Tok htok = token(s);
            uint32_t h = 0;
            if (!parse_hex32_tok(htok, h) || h == 0) return ParseErr::bad_args;   // <hash>: 8-hex key_hash32, nonzero
            Tok ptok = token(s);
            if (ptok.n == 0) return ParseErr::bad_args;                           // <l1,l2,…> required (no empty path)
            out = Command{};
            out.kind = CmdKind::send_layer;
            out.u.layer.dst_hash  = h;
            out.u.layer.hop_count = 0;
            out.u.layer.flags     = static_cast<uint8_t>(is_layer_ack ? DATA_FLAG_E2E_ACK_REQ : 0);
            // Split the comma-separated decimal destination layer ids into hops[]. Cap at gw_env_max_hops-1:
            // originate_layer_path PREPENDS our own layer as path[0], so the user supplies at most that many.
            uint32_t v = 0; bool digit = false;
            for (size_t i = 0; i < ptok.n; ++i) {
                const char ch = ptok.s[i];
                if (ch == ',') {
                    if (!digit || v == 0 || v > 255) return ParseErr::bad_args;   // empty / zero / >255 element
                    if (out.u.layer.hop_count >= protocol::gw_env_max_hops - 1) return ParseErr::bad_args;  // too many hops
                    out.u.layer.hops[out.u.layer.hop_count++] = static_cast<uint8_t>(v);
                    v = 0; digit = false;
                } else if (ch >= '0' && ch <= '9') {
                    v = v * 10 + static_cast<uint32_t>(ch - '0');
                    if (v > 255) return ParseErr::bad_args;
                    digit = true;
                } else {
                    return ParseErr::bad_args;                                    // non-numeric
                }
            }
            if (!digit || v == 0 || v > 255) return ParseErr::bad_args;           // the final element
            if (out.u.layer.hop_count >= protocol::gw_env_max_hops - 1) return ParseErr::bad_args;
            out.u.layer.hops[out.u.layer.hop_count++] = static_cast<uint8_t>(v);
            // body = remainder after exactly one separating space (verbatim, incl. spaces).
            if (s.p < s.end && (*s.p == ' ' || *s.p == '\t')) ++s.p;
            size_t body_len = static_cast<size_t>(s.end - s.p);
            if (body_len > protocol::max_payload_bytes_hard_cap) body_len = protocol::max_payload_bytes_hard_cap;
            out.body = reinterpret_cast<const uint8_t*>(s.p);
            out.body_len = static_cast<uint8_t>(body_len);
            return ParseErr::ok;
        }
    }

    //   send <id> <text>           — DM by short id, NO E2E ack
    //   send_ack <id> <text>       — DM by short id, E2E ack requested (flags DATA_FLAG_E2E_ACK_REQ = 0x10)
    //   sendhash <hash> <text>     — DM by key_hash32 (hash-locate); on_command resolves then sends
    //   sendhash_ack <hash> <text> — DM by key_hash32, E2E ack requested
    //   send_channel <ch> <text>   — single-layer channel gossip (channel_id 0..255)
    const bool is_send         = tok_eq(verb, "send");
    const bool is_send_ack     = tok_eq(verb, "send_ack");
    const bool is_sendhash     = tok_eq(verb, "sendhash");
    const bool is_sendhash_ack = tok_eq(verb, "sendhash_ack");
    const bool is_channel      = tok_eq(verb, "send_channel");
    if (!is_send && !is_send_ack && !is_sendhash && !is_sendhash_ack && !is_channel)
        return ParseErr::unknown_verb;
    const bool by_hash = is_sendhash || is_sendhash_ack;   // first arg is a hex key_hash32, not a decimal id
    const bool e2e_ack = is_send_ack || is_sendhash_ack;   // the *_ack verbs request the E2E ack

    // first arg: hex key_hash32 (sendhash*), decimal channel id (send_channel, 0..255), or decimal dst id (0..254).
    Tok arg = token(s);
    uint32_t arg_val = 0;
    if (by_hash) { if (!parse_hex32_tok(arg, arg_val))                         return ParseErr::bad_args; }
    else         { if (!parse_u32_tok(arg, is_channel ? 255u : 254u, arg_val)) return ParseErr::bad_args; }

    // body = remainder after exactly one separating space (verbatim, incl. spaces).
    if (s.p < s.end && (*s.p == ' ' || *s.p == '\t')) ++s.p;
    size_t body_len = static_cast<size_t>(s.end - s.p);
    if (body_len > protocol::max_payload_bytes_hard_cap) body_len = protocol::max_payload_bytes_hard_cap;

    out = Command{};
    if (is_channel) {
        out.kind = CmdKind::send_channel;
        out.u.channel.channel_id = static_cast<uint8_t>(arg_val);
    } else {
        out.kind = CmdKind::send;
        out.u.send.dst_id   = by_hash ? 0 : static_cast<uint8_t>(arg_val);
        out.u.send.dst_hash = by_hash ? arg_val : 0u;     // on_command (node.cpp) routes dst_hash!=0 to send_by_hash
        out.u.send.flags    = static_cast<uint8_t>(e2e_ack ? DATA_FLAG_E2E_ACK_REQ : 0);  // *_ack => the wire bit the RX acts on (node_mac_rx: & 0x10). 0x08 was a DEAD bit -> acks never fired.
    }
    out.body = reinterpret_cast<const uint8_t*>(s.p);
    out.body_len = static_cast<uint8_t>(body_len);
    return ParseErr::ok;
}

CfgErr parse_cfg(const char* line, size_t len, NodeConfig& cfg,
                 uint8_t& node_id, uint32_t& key_hash32) {
    Scan s{ line, line + len };
    Tok verb = token(s);
    if (!tok_eq(verb, "cfg")) return CfgErr::unknown_key;  // only cfg lines reach here
    Tok key = token(s);
    Tok val = token(s);

    uint32_t u = 0;
    if (tok_eq(key, "id")) {
        if (!parse_u32_tok(val, 254, u)) return CfgErr::bad_value;
        node_id = static_cast<uint8_t>(u);
    } else if (tok_eq(key, "key")) {
        if (!parse_hex32_tok(val, key_hash32)) return CfgErr::bad_value;
    } else if (tok_eq(key, "routing_sf")) {
        if (!parse_u32_tok(val, 12, u) || u < 5) return CfgErr::bad_value;
        cfg.routing_sf = static_cast<uint8_t>(u);
    // `gateway` is NO LONGER a cfg key: is_gateway is DERIVED = (n_layers==2) in on_init (a gateway is a dedicated
    // dual-layer firmware build). An operator cannot set it; it falls through to unknown_key below.
    } else if (tok_eq(key, "gateway_only")) {                  // §7: legacy single-layer pure-bridge flag — now DEAD
                                                               // (only read under is_gateway, which single-layer never is); kept read-only
        if (tok_eq(val, "1") || tok_eq(val, "true")) cfg.gateway_only = true;
        else if (tok_eq(val, "0") || tok_eq(val, "false")) cfg.gateway_only = false;
        else return CfgErr::bad_value;
    } else if (tok_eq(key, "beacon_period_ms")) {
        if (!parse_u32_tok(val, 0xFFFFFFFFu, u)) return CfgErr::bad_value;
        cfg.beacon_period_ms = u;
    } else if (tok_eq(key, "leaf_id")) {
        if (!parse_u32_tok(val, 254, u)) return CfgErr::bad_value;
        cfg.leaf_id = static_cast<uint8_t>(u);
    } else {
        return CfgErr::unknown_key;
    }
    return CfgErr::ok;
}

}  // namespace meshroute::console
