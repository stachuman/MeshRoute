// MeshRoute — lib/console/console_json.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
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
        case CmdCode::err_unprovisioned:   return "err_unprovisioned";   // node_id==0 (very common on a fresh device)
        case CmdCode::err_no_data_sf:      return "err_no_data_sf";      // allowed_sf_bitmap==0 (sf_list unset)
    }
    return "err_unknown";
}
const char* pushkind_name(PushKind k) {
    switch (k) {
        case PushKind::msg_recv:      return "msg_recv";
        case PushKind::channel_recv:  return "channel_recv";
        case PushKind::send_acked:    return "send_acked";
        case PushKind::send_failed:   return "send_failed";
        case PushKind::hash_resolved: return "hash_resolved";
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
    // Clamp to the array bound: Push.body is uint8_t[max_payload_bytes_hard_cap]. body_len is set from validated
    // frame data upstream, but a defensive clamp here means a corrupt value can never drive str() to read past
    // the buffer (an OOB read would be a far worse failure than a truncated body).
    const size_t body_n = p.body_len <= protocol::max_payload_bytes_hard_cap ? p.body_len
                                                                             : protocol::max_payload_bytes_hard_cap;
    if (p.kind == PushKind::msg_recv) {
        j.lit(",\"origin\":");      j.u32(p.origin);
        j.lit(",\"ctr\":");         j.u32(p.ctr);
        j.lit(",\"sender_hash\":"); j.u32(p.sender_hash);   // Phase-3: live↔pulled DM dedup identity (0 if no SOURCE_HASH)
        j.lit(",\"body\":");        j.str(reinterpret_cast<const char*>(p.body), body_n);
    } else if (p.kind == PushKind::channel_recv) {
        j.lit(",\"origin\":");         j.u32(p.origin);
        j.lit(",\"channel_id\":");     j.u32(p.channel_id);
        j.lit(",\"channel_msg_id\":"); j.u32(p.channel_msg_id);   // Phase-3: the full 32-bit channel dedup identity
        j.lit(",\"body\":");           j.str(reinterpret_cast<const char*>(p.body), body_n);
    } else if (p.kind == PushKind::hash_resolved) {
        const uint32_t hash = static_cast<uint32_t>(p.body[0]) | (static_cast<uint32_t>(p.body[1]) << 8)
                            | (static_cast<uint32_t>(p.body[2]) << 16) | (static_cast<uint32_t>(p.body[3]) << 24);
        j.lit(",\"node\":"); j.u32(p.origin);          // 0 = unresolved / timeout
        j.lit(",\"auth\":"); j.u32(p.dst);
        j.lit(",\"hash\":"); j.u32(hash);
    } else {  // send_acked / send_failed
        j.lit(",\"dst\":"); j.u32(p.dst);
        j.lit(",\"ctr\":"); j.u32(p.ctr);
    }
    j.ch('}');
    return j.finish();
}
size_t write_log(char* buf, size_t cap, const char* msg) {
    JsonBuf j(buf, cap);
    j.lit("{\"log\":"); j.str(msg ? msg : "", msg ? std::strlen(msg) : 0); j.ch('}');
    return j.finish();
}
size_t write_err(char* buf, size_t cap, const char* code, const char* msg) {
    JsonBuf j(buf, cap);
    j.lit("{\"err\":"); j.str(code, std::strlen(code));
    if (msg) { j.lit(",\"msg\":"); j.str(msg, std::strlen(msg)); }
    j.ch('}');
    return j.finish();
}
static void key_hex32(JsonBuf& j, uint32_t key) {
    char t[16]; std::snprintf(t, sizeof t, "\"%08x\"", key); j.lit(t);
}
size_t write_ready(char* buf, size_t cap, uint8_t id, uint32_t key, const NodeConfig& c, const char* mode,
                   uint32_t inbox_epoch) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"ready\",\"id\":"); j.u32(id);
    j.lit(",\"key\":"); key_hex32(j, key);
    j.lit(",\"leaf_id\":"); j.u32(c.leaf_id);
    j.lit(",\"mode\":"); j.str(mode, std::strlen(mode));
    j.lit(",\"gateway\":"); j.lit(c.is_gateway ? "true" : "false");
    j.lit(",\"routing_sf\":"); j.u32(c.routing_sf);
    j.lit(",\"inbox_epoch\":"); j.u32(inbox_epoch);   // Phase-3: bumps on any store wipe -> app re-pulls from 0
    j.ch('}');
    return j.finish();
}

// ---- Phase-3 inbox sync: the pulled-record stream + the pull terminator + the mark_read ack ----------------
// Schema = ios-companion/INBOX_SYNC_CONTRACT.md. sender_hash / channel_msg_id are DECIMAL u32 (not hex). rx_ms
// is node uptime (the app stamps wall-clock on pull). Fields are passed individually so console_json stays free
// of an inbox.h dependency. Bodies are JSON-escaped + bounded like write_push.
size_t write_inbox_dm(char* buf, size_t cap, uint32_t seq, uint8_t origin, uint16_t ctr, uint32_t sender_hash,
                      uint64_t rx_ms, const char* body, size_t body_len) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"inbox_dm\",\"seq\":"); j.u32(seq);
    j.lit(",\"origin\":");      j.u32(origin);
    j.lit(",\"ctr\":");         j.u32(ctr);
    j.lit(",\"sender_hash\":"); j.u32(sender_hash);
    j.lit(",\"rx_ms\":");       j.i64(static_cast<int64_t>(rx_ms));
    j.lit(",\"body\":");        j.str(body, body_len);
    j.ch('}');
    return j.finish();
}
size_t write_inbox_channel(char* buf, size_t cap, uint32_t seq, uint8_t origin, uint8_t channel_id,
                           uint32_t channel_msg_id, uint64_t rx_ms, const char* body, size_t body_len) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"inbox_channel\",\"seq\":"); j.u32(seq);
    j.lit(",\"origin\":");         j.u32(origin);
    j.lit(",\"channel_id\":");     j.u32(channel_id);
    j.lit(",\"channel_msg_id\":"); j.u32(channel_msg_id);
    j.lit(",\"rx_ms\":");          j.i64(static_cast<int64_t>(rx_ms));
    j.lit(",\"body\":");           j.str(body, body_len);
    j.ch('}');
    return j.finish();
}
size_t write_inbox_end(char* buf, size_t cap, uint32_t dm_seq, uint32_t chan_seq, uint32_t epoch, uint32_t count) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"inbox_end\",\"dm_seq\":"); j.u32(dm_seq);
    j.lit(",\"chan_seq\":"); j.u32(chan_seq);
    j.lit(",\"epoch\":");    j.u32(epoch);
    j.lit(",\"count\":");    j.u32(count);
    j.ch('}');
    return j.finish();
}
size_t write_inbox_marked(char* buf, size_t cap, const char* kind, uint32_t seq) {
    JsonBuf j(buf, cap);
    j.lit("{\"ack\":\"mark_read\",\"kind\":"); j.str(kind, std::strlen(kind));
    j.lit(",\"seq\":"); j.u32(seq);
    j.ch('}');
    return j.finish();
}
size_t write_status(char* buf, size_t cap, uint8_t id, uint32_t key, const NodeConfig& c, const char* state) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"status\",\"id\":"); j.u32(id);
    j.lit(",\"key\":"); key_hex32(j, key);
    j.lit(",\"state\":"); j.str(state, std::strlen(state));
    j.lit(",\"leaf_id\":"); j.u32(c.leaf_id);
    j.lit(",\"gateway\":"); j.lit(c.is_gateway ? "true" : "false");
    j.lit(",\"routing_sf\":"); j.u32(c.routing_sf);
    j.ch('}');
    return j.finish();
}

}  // namespace meshroute::console
