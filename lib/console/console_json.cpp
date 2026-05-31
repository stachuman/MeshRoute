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

const char* cmdcode_name(CmdCode c) {
    switch (c) {
        case CmdCode::queued:              return "queued";
        case CmdCode::err_unknown_dst:     return "err_unknown_dst";
        case CmdCode::err_too_large:       return "err_too_large";
        case CmdCode::err_no_gateway:      return "err_no_gateway";
        case CmdCode::err_priority_capped: return "err_priority_capped";
        case CmdCode::err_no_binding:      return "err_no_binding";
        case CmdCode::err_unsupported:     return "err_unsupported";
    }
    return "err_unknown";
}
const char* pushkind_name(PushKind k) {
    switch (k) {
        case PushKind::msg_recv:    return "msg_recv";
        case PushKind::send_acked:  return "send_acked";
        case PushKind::send_failed: return "send_failed";
    }
    return "unknown";
}
size_t write_ack(char* buf, size_t cap, const CmdResult& r) {
    JsonBuf j(buf, cap);
    j.lit("{\"ack\":\""); j.lit(cmdcode_name(r.code)); j.ch('"');
    j.lit(",\"ctr\":"); j.u32(r.ctr);
    j.lit(",\"qd\":");  j.u32(r.queue_depth);
    j.ch('}');
    return j.finish();
}
size_t write_event(char* buf, size_t cap, const char* type, const EventField* f, size_t n) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\""); j.lit(type); j.ch('"');
    for (size_t i = 0; i < n; ++i) {
        j.ch(','); j.key(f[i].key);
        switch (f[i].type) {
            case EventField::T::i64:     j.i64(f[i].i); break;
            case EventField::T::f64:     j.f64(f[i].f); break;
            case EventField::T::str:     j.str(f[i].s ? f[i].s : "", f[i].s ? std::strlen(f[i].s) : 0); break;
            case EventField::T::boolean: j.lit(f[i].b ? "true" : "false"); break;
        }
    }
    j.ch('}');
    return j.finish();
}
size_t write_push(char* buf, size_t cap, const Push& p) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\""); j.lit(pushkind_name(p.kind)); j.ch('"');
    if (p.kind == PushKind::msg_recv) {
        j.lit(",\"origin\":"); j.u32(p.origin);
        j.lit(",\"ctr\":");    j.u32(p.ctr);
        j.lit(",\"body\":");   j.str(reinterpret_cast<const char*>(p.body), p.body_len);
    } else {  // send_acked / send_failed
        j.lit(",\"dst\":"); j.u32(p.dst);
        j.lit(",\"ctr\":"); j.u32(p.ctr);
    }
    j.ch('}');
    return j.finish();
}

}  // namespace meshroute::console
