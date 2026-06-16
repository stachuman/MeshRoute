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
void JsonBuf::i64(int64_t v) {
    // Hand-rolled digits: newlib-nano (the nRF52 BSP libc) has an integer-only printf with NO long-long
    // support — "%lld" emits the literal "ld" on metal (host libcs hide this), producing invalid JSON.
    char t[24]; char* p = t + sizeof t; *--p = '\0';
    uint64_t u = static_cast<uint64_t>(v); if (v < 0) u = ~u + 1;   // magnitude; INT64_MIN-safe
    do { *--p = static_cast<char>('0' + u % 10); u /= 10; } while (u);
    if (v < 0) *--p = '-';
    lit(p);
}
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
        case PushKind::peer_key_cached: return "peer_key_cached";
    }
    return "unknown";
}
// E2E §5: send_failed.reason — the app maps no_pubkey -> "Request key / Scan QR"; the permanent reasons -> plain fail.
const char* sendfailreason_name(SendFailReason r) {
    switch (r) {
        case SendFailReason::no_pubkey:   return "no_pubkey";
        case SendFailReason::no_identity: return "no_identity";
        case SendFailReason::too_large:   return "too_large";
        case SendFailReason::bad_rng:     return "bad_rng";
        case SendFailReason::no_route:    return "no_route";
        case SendFailReason::none:        return "none";
    }
    return "none";
}
size_t write_ack(char* buf, size_t cap, const CmdResult& r) {
    JsonBuf j(buf, cap);
    j.lit("{\"ack\":\""); j.lit(cmdcode_name(r.code)); j.ch('"');
    j.lit(",\"ctr\":"); j.u32(r.ctr);
    j.lit(",\"qd\":");  j.u32(r.queue_depth);
    // The "send handle" (CmdResult.dst_hash / layer_path): dh != 0 => a hash/layer-addressed send (correlate by
    // dh, never the 8-bit id); lp != 0 => the send_layer destination path packed MSB-first ([2,3] -> 0x0203).
    j.lit(",\"dh\":"); j.u32(r.dst_hash);
    j.lit(",\"lp\":"); j.u32(r.layer_path);
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
        j.lit(",\"layer_id\":");    j.u32(p.layer_id);      // §2/Q13: which layer this DM arrived on (matches the pulled inbox_dm)
        j.lit(",\"ctr\":");         j.u32(p.ctr);
        j.lit(",\"sender_hash\":"); j.u32(p.sender_hash);   // Phase-3: live↔pulled DM dedup identity (0 if no SOURCE_HASH)
        if (p.seq) { j.lit(",\"seq\":"); j.u32(p.seq); }    // model B: the inbox seq (gap detector). OMITTED if 0 = inbox disabled
        if (p.enc) j.lit(",\"enc\":true");                  // §8b: this DM was delivered SEALED; omitted (=false) for plaintext
        j.lit(",\"body\":");        j.str(reinterpret_cast<const char*>(p.body), body_n);
    } else if (p.kind == PushKind::channel_recv) {
        j.lit(",\"origin\":");         j.u32(p.origin);
        j.lit(",\"layer_id\":");       j.u32(p.layer_id);   // §2/Q13: which layer this channel message arrived on
        j.lit(",\"channel_id\":");     j.u32(p.channel_id);
        j.lit(",\"channel_msg_id\":"); j.u32(p.channel_msg_id);   // Phase-3: the full 32-bit channel dedup identity
        if (p.seq) { j.lit(",\"seq\":"); j.u32(p.seq); }          // model B: the inbox seq (gap detector). OMITTED if 0 = inbox disabled
        j.lit(",\"body\":");           j.str(reinterpret_cast<const char*>(p.body), body_n);
    } else if (p.kind == PushKind::hash_resolved) {
        const uint32_t hash = static_cast<uint32_t>(p.body[0]) | (static_cast<uint32_t>(p.body[1]) << 8)
                            | (static_cast<uint32_t>(p.body[2]) << 16) | (static_cast<uint32_t>(p.body[3]) << 24);
        j.lit(",\"node\":"); j.u32(p.origin);          // 0 = unresolved / timeout
        j.lit(",\"auth\":"); j.u32(p.dst);
        j.lit(",\"hash\":"); j.u32(hash);
    } else if (p.kind == PushKind::peer_key_cached) {      // E2E §7: a recipient key arrived -> the app can resend encrypted
        j.lit(",\"hash\":");   j.u32(p.sender_hash);
        j.lit(",\"pinned\":false");                        // on-air (TOFU); a QR import is the separate peerkey_set ack (pinned:true)
    } else {  // send_acked / send_failed
        j.lit(",\"dst\":"); j.u32(p.dst);
        j.lit(",\"ctr\":"); j.u32(p.ctr);
        if (p.kind == PushKind::send_failed && p.reason != SendFailReason::none) {   // omit for a legacy/non-e2e giveup
            j.lit(",\"reason\":\""); j.lit(sendfailreason_name(p.reason)); j.ch('"'); }
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
// Dual-layer gateway: ADDITIVE per-leaf array (companion cfg/ready/status). Emitted ONLY when n_layers==2, so a
// single-layer node's JSON is byte-identical to before. One object per leaf (node_id/layer_id/routing_sf + the
// possibly-derived window_ms/offset of the ACTIVE config — on_init has already filled the derived split).
static void write_layers_array(JsonBuf& j, const NodeConfig& c) {
    if (c.n_layers != 2) return;
    j.lit(",\"layers\":[");
    for (uint8_t i = 0; i < 2; ++i) {
        const LayerConfig& L = c.layers[i];
        if (i) j.ch(',');
        j.lit("{\"layer_id\":");        j.u32(L.layer_id);
        j.lit(",\"node_id\":");         j.u32(L.node_id);
        j.lit(",\"routing_sf\":");      j.u32(L.routing_sf);
        j.lit(",\"allowed_sf_bitmap\":"); j.u32(L.allowed_sf_bitmap);
        j.lit(",\"beacon_period_ms\":"); j.u32(L.beacon_period_ms);
        j.lit(",\"window_period_ms\":"); j.u32(L.window_period_ms);
        j.lit(",\"window_ms\":");       j.u32(L.window_ms);
        j.lit(",\"window_offset_ms\":"); j.u32(L.window_offset_ms);
        j.ch('}');
    }
    j.ch(']');
}
size_t write_ready(char* buf, size_t cap, uint8_t id, uint32_t key, const NodeConfig& c, const char* mode,
                   uint32_t inbox_epoch, uint64_t now_ms, const char* name, size_t name_len, const uint8_t* ed_pub) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"ready\",\"id\":"); j.u32(id);
    j.lit(",\"key\":"); key_hex32(j, key);
    if (ed_pub) {                                                          // §4: the full pubkey (64 hex) for the QR `p` — key_hash32 alone can't seal
        j.lit(",\"pubkey\":\"");
        for (int i = 0; i < 32; ++i) { const char* H = "0123456789abcdef"; j.ch(H[ed_pub[i] >> 4]); j.ch(H[ed_pub[i] & 0xF]); }
        j.ch('"');
    }
    if (name && name_len) { j.lit(",\"name\":"); j.str(name, name_len); }   // §1.3 app-level identity label
    j.lit(",\"leaf_id\":"); j.u32(c.leaf_id);
    j.lit(",\"mode\":"); j.str(mode, std::strlen(mode));
    j.lit(",\"gateway\":"); j.lit(c.is_gateway ? "true" : "false");
    j.lit(",\"routing_sf\":"); j.u32(c.routing_sf);
    j.lit(",\"inbox_epoch\":"); j.u32(inbox_epoch);   // Phase-3: bumps on any store wipe -> app re-pulls from 0
    j.lit(",\"now_ms\":"); j.i64(static_cast<int64_t>(now_ms));  // node uptime at emit: the app's rx_ms->wall-clock anchor (no RTC)
    write_layers_array(j, c);                         // dual-layer gateway: additive "layers":[...] (omitted when n_layers==1)
    j.ch('}');
    return j.finish();
}

// ---- Phase-3 inbox sync: the pulled-record stream + the pull terminator + the mark_read ack ----------------
// Schema = ios-companion/INBOX_SYNC_CONTRACT.md. sender_hash / channel_msg_id are DECIMAL u32 (not hex). rx_ms
// is node uptime (the app stamps wall-clock on pull). Fields are passed individually so console_json stays free
// of an inbox.h dependency. Bodies are JSON-escaped + bounded like write_push.
size_t write_inbox_dm(char* buf, size_t cap, uint32_t seq, uint8_t origin, uint8_t layer_id, uint16_t ctr,
                      uint32_t sender_hash, uint64_t rx_ms, const char* body, size_t body_len, bool enc) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"inbox_dm\",\"seq\":"); j.u32(seq);
    j.lit(",\"origin\":");      j.u32(origin);
    j.lit(",\"layer_id\":");    j.u32(layer_id);   // §2/Q13: which layer this DM arrived on
    j.lit(",\"ctr\":");         j.u32(ctr);
    j.lit(",\"sender_hash\":"); j.u32(sender_hash);
    j.lit(",\"rx_ms\":");       j.i64(static_cast<int64_t>(rx_ms));
    if (enc) j.lit(",\"enc\":true");                  // §8b: sealed-delivery flag; omitted (=false) for plaintext
    j.lit(",\"body\":");        j.str(body, body_len);
    j.ch('}');
    return j.finish();
}
size_t write_inbox_channel(char* buf, size_t cap, uint32_t seq, uint8_t origin, uint8_t layer_id, uint8_t channel_id,
                           uint32_t channel_msg_id, uint64_t rx_ms, const char* body, size_t body_len) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"inbox_channel\",\"seq\":"); j.u32(seq);
    j.lit(",\"origin\":");         j.u32(origin);
    j.lit(",\"layer_id\":");       j.u32(layer_id);   // §2/Q13: which layer this channel message arrived on
    j.lit(",\"channel_id\":");     j.u32(channel_id);
    j.lit(",\"channel_msg_id\":"); j.u32(channel_msg_id);
    j.lit(",\"rx_ms\":");          j.i64(static_cast<int64_t>(rx_ms));
    j.lit(",\"body\":");           j.str(body, body_len);
    j.ch('}');
    return j.finish();
}
size_t write_inbox_end(char* buf, size_t cap, uint32_t dm_seq, uint32_t chan_seq, uint32_t epoch, uint32_t count,
                       uint64_t now_ms) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"inbox_end\",\"dm_seq\":"); j.u32(dm_seq);
    j.lit(",\"chan_seq\":"); j.u32(chan_seq);
    j.lit(",\"epoch\":");    j.u32(epoch);
    j.lit(",\"count\":");    j.u32(count);
    j.lit(",\"now_ms\":");   j.i64(static_cast<int64_t>(now_ms));  // uptime at emit, pairs with each record's rx_ms
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
size_t write_status(char* buf, size_t cap, uint8_t id, uint32_t key, const NodeConfig& c, const char* state,
                    const StatusFields& s) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"status\",\"id\":"); j.u32(id);
    j.lit(",\"key\":"); key_hex32(j, key);
    j.lit(",\"state\":"); j.str(state, std::strlen(state));
    j.lit(",\"leaf_id\":"); j.u32(c.leaf_id);
    j.lit(",\"gateway\":"); j.lit(c.is_gateway ? "true" : "false");
    j.lit(",\"routing_sf\":"); j.u32(c.routing_sf);
    j.lit(",\"uptime_ms\":"); j.i64(static_cast<int64_t>(s.uptime_ms));
    j.lit(",\"duty_ms\":");   j.u32(s.duty_ms);
    j.lit(",\"txq\":");       j.u32(s.txq);
    j.lit(",\"txdrop\":");    j.u32(s.txdrop);
    j.lit(",\"rx\":");        j.u32(s.rx);
    j.lit(",\"tx\":");        j.u32(s.tx);
    j.lit(",\"routes\":");    j.u32(s.routes);
    j.lit(",\"pending\":");   j.lit(s.pending ? "true" : "false");
    j.lit(",\"lbt\":");       j.lit(s.lbt ? "true" : "false");
    if (s.batt_mv >= 0) { j.lit(",\"batt_mv\":"); j.u32(static_cast<uint32_t>(s.batt_mv)); }
    write_layers_array(j, c);   // dual-layer gateway: additive "layers":[...] (omitted when n_layers==1)
    j.ch('}');
    return j.finish();
}

size_t write_route(char* buf, size_t cap, const RouteRow& r) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"route\",\"dest\":"); j.u32(r.dest);
    j.lit(",\"next\":");  j.u32(r.next);
    j.lit(",\"hops\":");  j.u32(r.hops);
    j.lit(",\"score\":"); j.i64(r.score);            // Q4 dB, may be negative
    j.lit(",\"gw\":");    j.lit(r.gw ? "true" : "false");
    j.lit(",\"layer\":"); j.u32(r.layer);
    j.lit(",\"age_ms\":"); j.u32(r.age_ms);
    j.lit(",\"cand\":");  j.u32(r.cand);
    j.ch('}');
    return j.finish();
}
size_t write_routes_end(char* buf, size_t cap, uint32_t count) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"routes_end\",\"count\":"); j.u32(count);
    j.ch('}');
    return j.finish();
}

// allowed_sf_bitmap → a quoted CSV "7,12" (bit position = SF); "" when unconfigured.
static void sf_list_str(JsonBuf& j, uint16_t bitmap) {
    j.ch('"');
    bool first = true;
    for (uint8_t sf = 5; sf <= 12; ++sf)
        if (bitmap & (1u << sf)) { if (!first) j.ch(','); j.u32(sf); first = false; }
    j.ch('"');
}
size_t write_cfg(char* buf, size_t cap, const NodeConfig& c, const CfgExtras& x) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"cfg\",\"node_id\":"); j.u32(x.node_id);
    j.lit(",\"freq_hz\":");    j.u32(x.freq_hz);
    j.lit(",\"routing_sf\":"); j.u32(c.routing_sf);
    j.lit(",\"sf_list\":");    sf_list_str(j, c.allowed_sf_bitmap);
    j.lit(",\"bw_hz\":");      j.u32(c.radio_bw_hz);
    j.lit(",\"cr\":");         j.u32(c.radio_cr);
    j.lit(",\"tx_power\":");   j.i64(x.tx_power);
    j.lit(",\"duty_x1000\":"); j.u32(x.duty_x1000);
    j.lit(",\"lbt\":");        j.lit(c.lbt_enabled ? "true" : "false");
    j.lit(",\"beacon_ms\":");  j.u32(c.beacon_period_ms);
    j.lit(",\"hop_cap\":");    j.u32(c.dv_hop_cap);
    j.lit(",\"leaf_id\":");    j.u32(c.leaf_id);
    j.lit(",\"gateway\":");    j.lit(c.is_gateway ? "true" : "false");
    j.lit(",\"mobile\":");     j.lit(c.is_mobile ? "true" : "false");
    j.lit(",\"ble_mode\":");   j.str(x.ble_mode, std::strlen(x.ble_mode));
    j.lit(",\"ble_period\":"); j.u32(x.ble_period);
    j.lit(",\"ble_pin\":");    j.u32(x.ble_pin);
    j.lit(",\"lat_e7\":");     j.i64(x.lat_e7);   // signed; degrees×1e7, 0 = unset
    j.lit(",\"lon_e7\":");     j.i64(x.lon_e7);
    write_layers_array(j, c);
    j.ch('}');
    return j.finish();
}

}  // namespace meshroute::console
