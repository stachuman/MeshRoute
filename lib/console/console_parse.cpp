// MeshRoute — lib/console/console_parse.cpp
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
    if (!tok_eq(verb, "send")) return ParseErr::unknown_verb;

    Tok dst = token(s);
    uint32_t dst_id = 0;
    if (!parse_u32_tok(dst, 254, dst_id)) return ParseErr::bad_args;

    // body = remainder after exactly one separating space (verbatim, incl. spaces).
    if (s.p < s.end && (*s.p == ' ' || *s.p == '\t')) ++s.p;
    size_t body_len = static_cast<size_t>(s.end - s.p);
    if (body_len > protocol::max_payload_bytes_hard_cap) body_len = protocol::max_payload_bytes_hard_cap;

    out = Command{};
    out.kind = CmdKind::send;
    out.u.send.dst_id = static_cast<uint8_t>(dst_id);
    out.u.send.dst_hash = 0;
    out.u.send.flags = 0x08;  // E2E (command.h: E2E=0x08); PRIORITY=0x02 deferred
    out.body = reinterpret_cast<const uint8_t*>(s.p);
    out.body_len = static_cast<uint8_t>(body_len);
    return ParseErr::ok;
}

}  // namespace meshroute::console
