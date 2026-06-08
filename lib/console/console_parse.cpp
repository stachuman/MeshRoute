// MeshRoute — lib/console/console_parse.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#include "console_parse.h"
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

}  // namespace

ParseErr parse_command(const char* line, size_t len, Command& out) {
    Scan s{ line, line + len };
    Tok verb = token(s);
    if (verb.n == 0) return ParseErr::empty;
    //   send <id> <text>          — DM, NO E2E ack
    //   send_ack <id> <text>      — DM, E2E ack requested (flags E2E=0x08)
    //   send_channel <ch> <text>  — single-layer channel gossip (channel_id 0..255)
    const bool is_send     = tok_eq(verb, "send");
    const bool is_send_ack = tok_eq(verb, "send_ack");
    const bool is_channel  = tok_eq(verb, "send_channel");
    if (!is_send && !is_send_ack && !is_channel) return ParseErr::unknown_verb;

    // first arg: dst short-id (send/send_ack, 0..254) or channel id (send_channel, 0..255).
    Tok arg = token(s);
    uint32_t arg_val = 0;
    if (!parse_u32_tok(arg, is_channel ? 255u : 254u, arg_val)) return ParseErr::bad_args;

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
        out.u.send.dst_id   = static_cast<uint8_t>(arg_val);
        out.u.send.dst_hash = 0;
        out.u.send.flags    = is_send_ack ? 0x08 : 0x00;  // send_ack = E2E ack-req; send = no ack (command.h: E2E=0x08)
    }
    out.body = reinterpret_cast<const uint8_t*>(s.p);
    out.body_len = static_cast<uint8_t>(body_len);
    return ParseErr::ok;
}

namespace {
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
    } else if (tok_eq(key, "gateway")) {
        if (tok_eq(val, "1") || tok_eq(val, "true")) cfg.is_gateway = true;
        else if (tok_eq(val, "0") || tok_eq(val, "false")) cfg.is_gateway = false;
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
