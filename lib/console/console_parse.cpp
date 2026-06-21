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

// Decode EXACTLY 2*n hex chars into out[0..n). false on a wrong length or a non-hex char (e.g. peerkey's 64-hex ed_pub).
bool parse_hex_bytes_tok(const Tok& t, uint8_t* out, size_t n) {
    if (t.n != 2 * n) return false;
    auto nib = [](char c, uint8_t& d) -> bool {
        if (c >= '0' && c <= '9') { d = static_cast<uint8_t>(c - '0');      return true; }
        if (c >= 'a' && c <= 'f') { d = static_cast<uint8_t>(10 + c - 'a'); return true; }
        if (c >= 'A' && c <= 'F') { d = static_cast<uint8_t>(10 + c - 'A'); return true; }
        return false;
    };
    for (size_t i = 0; i < n; ++i) {
        uint8_t hi, lo;
        if (!nib(t.s[2 * i], hi) || !nib(t.s[2 * i + 1], lo)) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

// §2 send cleanup: parse the trailing `[-a] [-e] "<body>"` (flags + a QUOTED body, in ANY order) from the scan cursor.
// `-a`=ack, `-e`=encrypt — each gated by allow_a/allow_e (an off-target flag is an error). The body is the verbatim
// text between the quotes (spaces allowed). Returns false on: a disallowed/unknown flag, an unquoted token, an
// unterminated/duplicate quote, or no body at all (the body is required).
bool parse_send_tail(Scan& s, bool allow_a, bool allow_e, bool& ack, bool& enc,
                     const uint8_t*& body, uint8_t& body_len) {
    ack = false; enc = false; body = nullptr; body_len = 0; bool body_seen = false;
    for (;;) {
        skip_ws(s);
        if (s.p >= s.end) break;
        if (*s.p == '"') {                                       // the quoted body
            if (body_seen) return false;                         // two bodies
            ++s.p;
            const char* b = s.p;
            while (s.p < s.end && *s.p != '"') ++s.p;
            if (s.p >= s.end) return false;                      // unterminated quote
            size_t n = static_cast<size_t>(s.p - b);
            if (n > protocol::max_payload_bytes_hard_cap) n = protocol::max_payload_bytes_hard_cap;
            body = reinterpret_cast<const uint8_t*>(b); body_len = static_cast<uint8_t>(n);
            body_seen = true;
            ++s.p;                                               // past the closing quote
        } else if (*s.p == '-') {                                // a flag: -a / -e (lone, single-char)
            ++s.p;
            if (s.p >= s.end) return false;
            const char f = *s.p; ++s.p;
            if (s.p < s.end && *s.p != ' ' && *s.p != '\t') return false;   // must be a lone token
            if      (f == 'a') { if (!allow_a) return false; ack = true; }
            else if (f == 'e') { if (!allow_e) return false; enc = true; }
            else return false;                                   // unknown flag
        } else {
            return false;                                        // unquoted text -> error (body must be quoted)
        }
    }
    return body_seen;                                            // the quoted body is mandatory
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

    //   peerkey <ed_pub hex64> — §3: install a scanned peer's full pubkey as a PINNED (verified) key. hash = ed_pub[:4].
    if (tok_eq(verb, "peerkey")) {
        Tok arg = token(s);
        uint8_t ed[32];
        if (!parse_hex_bytes_tok(arg, ed, 32)) return ParseErr::bad_args;
        out = Command{};
        out.kind = CmdKind::peerkey;
        for (int i = 0; i < 32; ++i) out.u.peerkey.ed_pub[i] = ed[i];
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

    //   §2 send cleanup — 3 orthogonal verbs, QUOTED body, -a (ack) / -e (encrypt) flags in ANY order. HARD SWITCH:
    //   the old send_ack/sendhash/sendhash_ack/sendhashx/sendhashx_ack/send_layer_ack verbs are GONE (-> unknown_verb).
    //   send <id|hash> "<text>" [-a] [-e]          — id (<=254 dec) vs hash (8-hex) AUTO-detected; -e=crypt (hash only)
    //   send_channel <ch> "<text>"                 — channel gossip (no ack/enc)
    //   send_layer <hash> <l1,l2,…> "<text>" [-a]  — explicit cross-layer path
    {
        const bool is_send    = tok_eq(verb, "send");
        const bool is_channel = tok_eq(verb, "send_channel");
        const bool is_layer   = tok_eq(verb, "send_layer");
        if (!is_send && !is_channel && !is_layer) return ParseErr::unknown_verb;

        if (is_channel) {                                       // send_channel <ch> "<text>" — no ack/enc
            uint32_t ch = 0;
            if (!parse_u32_tok(token(s), 255u, ch)) return ParseErr::bad_args;
            bool ack = false, enc = false; const uint8_t* body = nullptr; uint8_t blen = 0;
            if (!parse_send_tail(s, /*allow_a=*/false, /*allow_e=*/false, ack, enc, body, blen)) return ParseErr::bad_args;
            out = Command{};
            out.kind = CmdKind::send_channel;
            out.u.channel.channel_id = static_cast<uint8_t>(ch);
            out.body = body; out.body_len = blen; out.crypt = CryptIntent::def;
            return ParseErr::ok;
        }

        if (is_layer) {                                        // send_layer <hash> <l1,l2,…> "<text>" [-a]
            uint32_t h = 0;
            if (!parse_hex32_tok(token(s), h) || h == 0) return ParseErr::bad_args;   // <hash>: key_hash32, nonzero
            Tok ptok = token(s);
            if (ptok.n == 0) return ParseErr::bad_args;                               // <l1,l2,…> required (no empty path)
            out = Command{};
            out.kind = CmdKind::send_layer;
            out.u.layer.dst_hash = h; out.u.layer.hop_count = 0;
            // comma-separated decimal layer ids -> hops[]; cap at gw_env_max_hops-1 (originate_layer_path prepends ours).
            uint32_t v = 0; bool digit = false;
            for (size_t i = 0; i < ptok.n; ++i) {
                const char ch = ptok.s[i];
                if (ch == ',') {
                    if (!digit || v == 0 || v > 255) return ParseErr::bad_args;
                    if (out.u.layer.hop_count >= protocol::gw_env_max_hops - 1) return ParseErr::bad_args;
                    out.u.layer.hops[out.u.layer.hop_count++] = static_cast<uint8_t>(v);
                    v = 0; digit = false;
                } else if (ch >= '0' && ch <= '9') {
                    v = v * 10 + static_cast<uint32_t>(ch - '0');
                    if (v > 255) return ParseErr::bad_args;
                    digit = true;
                } else {
                    return ParseErr::bad_args;
                }
            }
            if (!digit || v == 0 || v > 255) return ParseErr::bad_args;
            if (out.u.layer.hop_count >= protocol::gw_env_max_hops - 1) return ParseErr::bad_args;
            out.u.layer.hops[out.u.layer.hop_count++] = static_cast<uint8_t>(v);
            bool ack = false, enc = false; const uint8_t* body = nullptr; uint8_t blen = 0;
            if (!parse_send_tail(s, /*allow_a=*/true, /*allow_e=*/false, ack, enc, body, blen)) return ParseErr::bad_args;
            out.u.layer.flags = static_cast<uint8_t>(ack ? DATA_FLAG_E2E_ACK_REQ : 0);
            out.body = body; out.body_len = blen;
            return ParseErr::ok;
        }

        // send <id|hash> "<text>" [-a] [-e]: AUTO-detect — EXACTLY 8 hex chars => key_hash32; else decimal <=254 => id.
        Tok arg = token(s);
        uint32_t h = 0, id = 0; bool by_hash = false;
        if (arg.n == 8 && parse_hex32_tok(arg, h)) by_hash = true;          // 8-hex -> hash (an id is <=3 digits, never 8)
        else if (parse_u32_tok(arg, 254u, id))     by_hash = false;         // decimal <=254 -> id
        else return ParseErr::bad_args;
        bool ack = false, enc = false; const uint8_t* body = nullptr; uint8_t blen = 0;
        if (!parse_send_tail(s, /*allow_a=*/true, /*allow_e=*/by_hash, ack, enc, body, blen)) return ParseErr::bad_args;  // -e only on a hash target
        out = Command{};
        out.kind = CmdKind::send;
        out.u.send.dst_id   = by_hash ? 0 : static_cast<uint8_t>(id);
        out.u.send.dst_hash = by_hash ? h : 0u;            // on_command routes dst_hash!=0 to send_by_hash
        out.u.send.flags    = static_cast<uint8_t>(ack ? DATA_FLAG_E2E_ACK_REQ : 0);
        out.body = body; out.body_len = blen;
        out.crypt = enc ? CryptIntent::on : CryptIntent::def;   // -e => CRYPTED; absent => the node's e2e_dm default (force-plain dropped)
        return ParseErr::ok;
    }
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
