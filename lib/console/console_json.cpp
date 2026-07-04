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
// M9: how many bytes a WELL-FORMED UTF-8 sequence starting at s[i] spans (1..4), or 0 if the byte/sequence is
// malformed. This is the Unicode-standard "well-formed byte sequence" table (RFC 3629): it rejects overlong
// encodings, UTF-16 surrogates (U+D800..U+DFFF) and code points > U+10FFFF — not just a naive length-from-lead.
// A lone/truncated/out-of-range byte returns 0 -> the caller emits U+FFFD and advances one byte. Bytes are
// attacker-controlled DM/channel bodies; we PASS valid multi-byte UTF-8 through verbatim (a raw 0xC3 0xA9 = "é"
// is valid inside a JSON string) and only sanitize invalid bytes, so legit international text/emoji survive.
static size_t utf8_seq_len(const unsigned char* s, size_t remaining) {
    const unsigned char c0 = s[0];
    if (c0 < 0x80) return 1;                                  // ASCII (handled before this is called, but complete)
    auto cont = [](unsigned char b) { return b >= 0x80 && b <= 0xBF; };
    if (c0 >= 0xC2 && c0 <= 0xDF) {                           // 2-byte (0xC0/0xC1 = overlong -> rejected)
        if (remaining >= 2 && cont(s[1])) return 2;
        return 0;
    }
    if (c0 >= 0xE0 && c0 <= 0xEF) {                           // 3-byte
        if (remaining < 3) return 0;
        const unsigned char lo = (c0 == 0xE0) ? 0xA0 : 0x80;  // 0xE0: reject overlong
        const unsigned char hi = (c0 == 0xED) ? 0x9F : 0xBF;  // 0xED: reject surrogates
        if (s[1] >= lo && s[1] <= hi && cont(s[2])) return 3;
        return 0;
    }
    if (c0 >= 0xF0 && c0 <= 0xF4) {                           // 4-byte
        if (remaining < 4) return 0;
        const unsigned char lo = (c0 == 0xF0) ? 0x90 : 0x80;  // 0xF0: reject overlong
        const unsigned char hi = (c0 == 0xF4) ? 0x8F : 0xBF;  // 0xF4: reject > U+10FFFF
        if (s[1] >= lo && s[1] <= hi && cont(s[2]) && cont(s[3])) return 4;
        return 0;
    }
    return 0;                                                 // 0x80..0xC1, 0xF5..0xFF = never a valid lead
}
void JsonBuf::str(const char* s, size_t n) {
    ch('"');
    const unsigned char* u = reinterpret_cast<const unsigned char*>(s);
    for (size_t i = 0; i < n; ) {
        unsigned char c = u[i];
        switch (c) {
            case '"':  lit("\\\""); ++i; continue;
            case '\\': lit("\\\\"); ++i; continue;
            case '\n': lit("\\n");  ++i; continue;
            case '\r': lit("\\r");  ++i; continue;
            case '\t': lit("\\t");  ++i; continue;
        }
        if (c < 0x20) {                                       // C0 controls -> \u00xx (JSON requires it)
            char buf8[8]; std::snprintf(buf8, sizeof buf8, "\\u%04x", c); lit(buf8); ++i; continue;
        }
        if (c < 0x80) { ch(static_cast<char>(c)); ++i; continue; }   // ASCII (incl. 0x7F) -> verbatim
        // Multi-byte lead: emit the whole sequence verbatim iff WELL-FORMED, else one U+FFFD (EF BF BD) + skip 1.
        const size_t seq = utf8_seq_len(u + i, n - i);
        if (seq == 0) { ch(static_cast<char>(0xEF)); ch(static_cast<char>(0xBF)); ch(static_cast<char>(0xBD)); ++i; }
        else { for (size_t k = 0; k < seq; ++k) ch(static_cast<char>(u[i + k])); i += seq; }
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
        case PushKind::config_adopted:  return "config_adopted";   // R6.3: leaf-config membership update (live)
        case PushKind::join_refused:    return "join_refused";     // R6.3 §7c: wire-version / leaf-full refusal
        case PushKind::send_e2e_acked:  return "e2e_acked";        // §3: live twin of the durable inbox_dm type:"e2e_ack" (no more ev:"unknown")
        case PushKind::send_blocked:  return "send_blocked";       // Slice 6a: pre-TX self-gate feedback (cap / min-interval)
        case PushKind::channel_sent:  return "channel_sent";       // Slice 6c: OWN channel post re-offer outcome (relayed?)
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
        case SendFailReason::joining:     return "joining";   // R6.3: managed leaf not yet config-synced (transient — gate lifts on adopt)
        case SendFailReason::cap:          return "cap";          // Slice 6a: send_blocked — the per-origin cap
        case SendFailReason::min_interval: return "min_interval"; // Slice 6a: send_blocked — the burst floor
        case SendFailReason::no_cts:       return "no_cts";        // Slice 6b: DM giveup — CTS-timeout
        case SendFailReason::no_ack:       return "no_ack";        // Slice 6b: DM giveup — DATA-ACK-timeout
        case SendFailReason::none:        return "none";
    }
    return "none";
}
const char* joinrefusereason_name(JoinRefuseReason r) {   // R6.3 §7c
    switch (r) {
        case JoinRefuseReason::wire_version: return "wire_version";
        case JoinRefuseReason::leaf_full:    return "leaf_full";
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
size_t write_reqpubkey_sent(char* buf, size_t cap, uint32_t hash) {   // §2: the on-air pubkey request was flooded (replaces the generic {"ack":"queued"})
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"reqpubkey_sent\",\"hash\":"); j.u32(hash); j.ch('}');
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
size_t write_push(char* buf, size_t cap, const Push& p, const NodeConfig* cfg) {
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
    } else if (p.kind == PushKind::config_adopted) {       // R6.3: leaf-config adopted/updated -> the app's membership chip
        if (cfg) {
            j.lit(",\"lineage\":"); j.u32(cfg->lineage_id);
            j.lit(",\"epoch\":");   j.u32(cfg->config_epoch);
            if (cfg->leaf_name_len) { j.lit(",\"leaf\":"); j.str(cfg->leaf_name, cfg->leaf_name_len); }
            j.lit(",\"layer\":");   j.u32(cfg->leaf_id);   // ⚠ still the wire LEAF NIBBLE (the full 1..255 layer id is NV-side only; sending it needs NV plumbing — deferred, NOT the wire-load-bearing layers[0].layer_id)
        }
    } else if (p.kind == PushKind::join_refused) {         // R6.3 §7c: refusal feedback (invisible-on-metal telemetry replaced)
        j.lit(",\"reason\":\""); j.lit(joinrefusereason_name(p.join_reason)); j.ch('"');
        if (p.join_reason == JoinRefuseReason::wire_version) {
            j.lit(",\"their_ver\":"); j.u32(p.origin);
            j.lit(",\"my_ver\":");    j.u32(p.dst);
        }
    } else if (p.kind == PushKind::send_e2e_acked) {       // §3: the live twin of the durable inbox_dm type:"e2e_ack" — app marks its OUTBOX DELIVERED immediately
        j.lit(",\"origin\":");      j.u32(p.dst);          // the dest that CONFIRMED delivery (the -a DM's recipient; push carries it in .dst, node_mac_rx.cpp:610)
        j.lit(",\"ctr\":");         j.u32(p.ctr);          // the acked ctr
        j.lit(",\"sender_hash\":"); j.u32(p.sender_hash);  // the acker's key_hash32 (0 same-layer; set on a cross-layer ack). App matches (origin,ctr) or (sender_hash,ctr); NOT an inbound DM
    } else if (p.kind == PushKind::send_blocked) {   // Slice 6a: pre-TX self-gate feedback (kind/reason/next_ms)
        j.lit(",\"kind\":\""); j.lit(p.blocked_channel ? "channel" : "dm"); j.ch('"');
        j.lit(",\"reason\":\""); j.lit(sendfailreason_name(p.reason)); j.ch('"');
        j.lit(",\"next_ms\":"); j.u32(p.next_ms);
    } else if (p.kind == PushKind::channel_sent) {   // Slice 6c: origin re-offer outcome (relayed?)
        j.lit(",\"ctr\":"); j.u32(p.ctr);
        j.lit(",\"relayed\":"); j.lit(p.relayed ? "true" : "false");
        if (!p.relayed) j.lit(",\"reason\":\"no_relay\"");   // 1st-hop throttle or no neighbour
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
                   uint32_t inbox_epoch, uint64_t now_ms, const char* name, size_t name_len, const uint8_t* ed_pub,
                   uint8_t duty_pct, uint32_t duty_avail_ms) {
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
    // R6.3 leaf-config membership (iOS contract): lineage (0=unmanaged) / epoch / leaf name / layer (⚠ interim=wire leaf nibble, full 1..255 id is NV-side) / synced.
    j.lit(",\"lineage\":"); j.u32(c.lineage_id);
    j.lit(",\"epoch\":");   j.u32(c.config_epoch);
    if (c.leaf_name_len) { j.lit(",\"leaf\":"); j.str(c.leaf_name, c.leaf_name_len); }
    j.lit(",\"layer\":");   j.u32(c.leaf_id);
    j.lit(",\"synced\":");  j.lit((c.lineage_id == 0 || c.config_epoch > 0) ? "true" : "false");
    j.lit(",\"mode\":"); j.str(mode, std::strlen(mode));
    j.lit(",\"gateway\":"); j.lit(c.is_gateway ? "true" : "false");
    j.lit(",\"routing_sf\":"); j.u32(c.routing_sf);
    j.lit(",\"inbox_epoch\":"); j.u32(inbox_epoch);   // Phase-3: bumps on any store wipe -> app re-pulls from 0
    j.lit(",\"now_ms\":"); j.i64(static_cast<int64_t>(now_ms));  // node uptime at emit: the app's rx_ms->wall-clock anchor (no RTC)
    j.lit(",\"duty_pct\":"); j.u32(duty_pct);          // duty readout snapshot (refresh via the `duty` query); 100 -> the node is silent
    j.lit(",\"duty_avail_ms\":"); j.u32(duty_avail_ms);// ms until airtime ages back in (drives the app's silent-countdown banner)
    write_layers_array(j, c);                         // dual-layer gateway: additive "layers":[...] (omitted when n_layers==1)
    j.ch('}');
    return j.finish();
}

size_t write_duty(char* buf, size_t cap, uint8_t pct, uint32_t avail_ms, bool enabled) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"duty\",\"pct\":"); j.u32(pct);
    j.lit(",\"avail_ms\":"); j.u32(avail_ms);
    j.lit(",\"enabled\":"); j.lit(enabled ? "true" : "false");
    j.ch('}');
    return j.finish();
}

size_t write_limits(char* buf, size_t cap, const LimitsFields& L) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"limits\",\"win_ms\":");  j.u32(L.win_ms);
    j.lit(",\"win_left_ms\":");  j.u32(L.win_left_ms);
    j.lit(",\"n\":");            j.u32(L.n);
    j.lit(",\"ch_sf\":");        j.u32(L.ch_sf);
    j.lit(",\"ch_cap\":");       j.u32(L.ch_cap);
    j.lit(",\"ch_used\":");      j.u32(L.ch_used);
    j.lit(",\"ch_min_ms\":");    j.u32(L.ch_min_ms);
    j.lit(",\"ch_next_ms\":");   j.u32(L.ch_next_ms);
    j.lit(",\"ch_ceiling\":");   j.u32(L.ch_ceiling);
    j.lit(",\"dm_min_ms\":");    j.u32(L.dm_min_ms);
    j.lit(",\"dm_next_ms\":");   j.u32(L.dm_next_ms);
    j.lit(",\"duty_ms\":");      j.u32(L.duty_ms);
    j.lit(",\"duty_used_ms\":"); j.u32(L.duty_used_ms);
    j.ch('}');
    return j.finish();
}

// ---- Phase-3 inbox sync: the pulled-record stream + the pull terminator + the mark_read ack ----------------
// Schema = ios-companion/INBOX_SYNC_CONTRACT.md. sender_hash / channel_msg_id are DECIMAL u32 (not hex). rx_ms
// is node uptime (the app stamps wall-clock on pull). Fields are passed individually so console_json stays free
// of an inbox.h dependency. Bodies are JSON-escaped + bounded like write_push.
size_t write_inbox_dm(char* buf, size_t cap, uint32_t seq, uint8_t origin, uint8_t layer_id, uint16_t ctr,
                      uint32_t sender_hash, uint64_t rx_ms, const char* body, size_t body_len, bool enc, uint8_t type) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"inbox_dm\"");
    // The DATA_TYPE rides right after "ev". 0 = normal DM -> OMITTED (common case; wire-unchanged). 3 = DATA_TYPE_E2E_ACK
    // (frame_codec.h) -> a receipt -> "e2e_ack"; any other non-zero -> the numeric (never drop the distinction silently).
    if (type == 3)      j.lit(",\"type\":\"e2e_ack\"");
    else if (type != 0) { j.lit(",\"type\":"); j.u32(type); }
    j.lit(",\"seq\":");         j.u32(seq);
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
    j.lit(",\"leaf\":");  j.u32(r.leaf);             // the route's learned leaf nibble (layer & 0x0F)
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
