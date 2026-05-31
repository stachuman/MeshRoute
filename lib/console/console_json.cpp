// MeshRoute — lib/console/console_json.cpp
#include "console_json.h"
#include <cstdio>
#include <cstring>

namespace meshroute::console {

void JsonBuf::ch(char c) {
    if (overflow) return;
    if (pos + 1 >= cap) { overflow = true; return; }  // keep 1 byte for NUL
    buf[pos++] = c;
}
void JsonBuf::lit(const char* s) { while (*s) ch(*s++); }
void JsonBuf::str(const char* s, size_t n) {
    ch('"');
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c) {
            case '"':  lit("\\\""); break;
            case '\\': lit("\\\\"); break;
            case '\n': lit("\\n");  break;
            case '\r': lit("\\r");  break;
            case '\t': lit("\\t");  break;
            default:
                if (c < 0x20) { char u[8]; std::snprintf(u, sizeof u, "\\u%04x", c); lit(u); }
                else ch(static_cast<char>(c));
        }
    }
    ch('"');
}
void JsonBuf::key(const char* k) { ch('"'); lit(k); lit("\":"); }
void JsonBuf::i64(int64_t v) { char t[24]; std::snprintf(t, sizeof t, "%lld", static_cast<long long>(v)); lit(t); }
void JsonBuf::u32(uint32_t v) { char t[12]; std::snprintf(t, sizeof t, "%u", v); lit(t); }
void JsonBuf::f64(double v)  { char t[24]; std::snprintf(t, sizeof t, "%.4g", v); lit(t); }
size_t JsonBuf::finish() {
    ch('\n');
    if (overflow) return 0;
    buf[pos] = '\0';   // pos < cap guaranteed by ch()
    return pos;
}

}  // namespace meshroute::console
