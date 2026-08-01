// MeshRoute — lib/console/console_json.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#include "console_json.h"
#include <cstdio>
#include <cstring>

namespace meshroute::console {

static void key_hex32(JsonBuf& j, uint32_t key);   // fwd: quoted "%08x" hex (defined below; used by write_push team_id + write_ready/status/cfg)

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
void JsonBuf::f64(double v) {
    // L11: newlib-nano (the nRF52 device libc) has no %f/%g -> snprintf("%.4g") emits GARBAGE on device. Hand-rolled
    // fixed-point (<=4 dp, trailing zeros trimmed) matches %.4g for the small values f64 fields carry (SNR/dB/durations).
    if (v < 0) { ch('-'); v = -v; }
    if (v > 429496.0) v = 429496.0;                          // clamp so v*10000 fits u32 (f64 fields are never this large)
    const uint32_t scaled = static_cast<uint32_t>(v * 10000.0 + 0.5);
    u32(scaled / 10000);                                     // integer part
    const uint32_t frac = scaled % 10000;
    if (frac) {                                              // fractional part, trailing zeros trimmed (7.2500 -> 7.25)
        char d[4] = { char('0' + frac / 1000), char('0' + (frac / 100) % 10), char('0' + (frac / 10) % 10), char('0' + frac % 10) };
        int len = 4; while (len > 0 && d[len - 1] == '0') --len;
        ch('.'); for (int i = 0; i < len; ++i) ch(d[i]);
    }
}
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
        case CmdCode::err_ack_ring_full:   return "err_ack_ring_full";   // pending-ack ring saturated: a new -a send is REFUSED loudly (protocol_constants.h: NEVER evict-oldest)
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
        case PushKind::mobile_reg:    return "mobile_reg";         // §S2: mobile registration change (registered/home-lost)
        case PushKind::team_reg:      return "team_reg";           // §S2: team-DAD id adopted/re-picked
        case PushKind::join_adopted:  return "join_adopted";       // a DAD/join adopt landed (id may have changed)
        case PushKind::team_key_received: return "team_key_received";   // §team-ch-key T-K3: a teammate granted us the team CONTENT key over a sealed TYPE-19 DM (already adopted)
        case PushKind::team_channel_no_key: return "team_channel_no_key";   // §chan-crypt CL2a: a CRYPTED team channel post arrived and we cannot read it (no key, or a stale one) -> the app prompts for a grant. Rate-limited node-side.
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
        case SendFailReason::mobile_no_home:       return "mobile_no_home";        // §mobile: reply-expecting DM with no routable home (was MISSING here — rendered "none")
        case SendFailReason::gateway_unreachable:  return "gateway_unreachable";   // §3-A.5: gateway-doorstep hold gave up
        case SendFailReason::e2e_ack_timeout:      return "e2e_ack_timeout";     // §ack-deadline: a -a DM's e2e ack never returned inside the patience budget (delivery UNCONFIRMED, not failed)
        case SendFailReason::queue_full:           return "queue_full";          // §defer: the no-route defer queue was full -> the NEW send was refused (node_cascade.cpp defer_send)
        case SendFailReason::reprovisioned:        return "reprovisioned";       // §clean-join-carriers: a join/create/leave (or prep-restart) discarded this staged/in-flight DM — RE-ADDRESS before resending (the dst id belongs to the OLD network)
        case SendFailReason::unsealable:           return "unsealable";          // §team-ch-key T-K3: a sealed-only TYPE (the team key grant) on a transport that cannot carry it sealed-AND-typed (cross-layer / delegated) — PERMANENT for this route, grant from the target's own layer or over the team plane. §loc-per-send REUSES it for a `-l` send that would not be sealed (remedy: `-e` / `e2e_dm` / acquire the peer key)
        case SendFailReason::no_location:          return "no_location";         // §loc-per-send: `-l` asked to attach a position and this node has NO fix (lat_e7==0 && lon_e7==0) — the DM was NOT sent; remedy is a GPS fix or `cfg set lat`/`lon`, NOT encryption
        case SendFailReason::none:        return "none";
    }
    return "none";
}
// ★★ §AB2 (address-book spec 2026-07-29 §2.2): the peer-key CONFIDENCE level as an app-facing string. This is the field
// that makes the contract's existing rule — gate "send encrypted" on `conf >= authoritative`, NOT on key presence —
// actually checkable: before it, peer_key_cached emitted a hardcoded `"pinned":false` and `overheard` (key present,
// e2e_seal_inner will REFUSE) was indistinguishable from `authoritative` (can seal).
// Takes the ENUM, not the raw byte, so -Wswitch (gate-blocking since the 2026-07-25 ruling that came out of three
// enum→string defects) fails the build if a fourth level is ever added and not mapped here.
const char* peerkeyconf_name(Node::PeerKeyConf c) {
    switch (c) {
        case Node::PeerKeyConf::overheard:     return "overheard";       // cached on-air/on-pass — CANNOT seal to this peer
        case Node::PeerKeyConf::authoritative: return "authoritative";   // from the owner's own answer — CAN seal
        case Node::PeerKeyConf::pinned:        return "pinned";          // QR/manually verified — MITM-resistant, never aged or evicted
    }
    return "overheard";   // an out-of-range byte reads as the LEAST capable level (never claim a sealing capability we cannot back)
}
// ★★★ §AB4 (address-book spec 2026-07-29 §2.7.2): the TRUST ANCHOR of a retained position, as an app-facing string.
// This is the field that stops a map overstating a claim: a `team`-anchored position could have been written by ANY
// holder of the shared team content key (membership, not identity), while a `peer`-anchored one was sealed to us and
// opened with our key, so only that peer could have written it. The app must render the distinction.
// Takes the ENUM, so -Wswitch (gate-blocking since the 2026-07-25 ruling) fails the build if a third anchor is added
// and not mapped. ★ Only `peer` is producible today — see Node::PeerLocSrc for CL2, the `team` arm's named trigger.
const char* peerlocsrc_name(Node::PeerLocSrc s) {
    switch (s) {
        case Node::PeerLocSrc::peer: return "peer";   // PAIRWISE — sealed to us, opened with our key ⇒ "this specific peer"
        case Node::PeerLocSrc::team: return "team";   // GROUP — sealed to the shared team key ⇒ "some holder of the team key"
    }
    return "team";   // an out-of-range byte reads as the WEAKER anchor (never claim an attribution we cannot back — the
                     // mirror of peerkeyconf_name's least-capable policy, in this field's own honest direction)
}
const char* joinrefusereason_name(JoinRefuseReason r) {   // R6.3 §7c
    switch (r) {
        case JoinRefuseReason::wire_version: return "wire_version";
        case JoinRefuseReason::leaf_full:    return "leaf_full";
        case JoinRefuseReason::phy_mismatch:     return "phy_mismatch";       // §3-A.1/P2-1: team member refused a PHY-mismatched home (layer_id = the candidate's layer, dst = its routing_sf)
        case JoinRefuseReason::sf_list_mismatch: return "sf_list_mismatch";   // §3-A.1 ADVISORY: adopted, but configured vs host-offered sf_list low bytes disagree (origin = ours, dst = offered)
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
size_t write_join_started(char* buf, size_t cap, const JoinStartedFields& s) {   // the JSON verb ack for join/create
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"join_started\"");
    if (s.create) j.lit(",\"create\":true");
    j.lit(",\"layer\":"); j.u32(s.layer);
    j.lit(",\"leaf\":");  j.u32(s.leaf);
    if (s.create) {                                                  // create-only: lineage + the minted leaf name
        j.lit(",\"lineage\":");   j.u32(s.lineage);
        j.lit(",\"leaf_name\":"); j.str(s.leaf_name ? s.leaf_name : "", s.leaf_name ? s.leaf_name_len : 0);
    }
    j.lit(",\"freq_khz\":"); j.u32(s.freq_khz);
    j.lit(",\"sf\":");       j.u32(s.sf);
    j.lit(",\"bw_hz\":");    j.u32(s.bw_hz);
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
        if (p.origin_layer) { j.lit(",\"origin_layer\":"); j.u32(p.origin_layer); }   // §GapA: the SENDER's layer on a cross-layer DM -> the app's (layer,hash) reply address. OMITTED when 0 (same-layer/non-XL -> byte-identical)
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
        if (p.team_id) { j.lit(",\"team_id\":"); key_hex32(j, p.team_id); }   // §S4: team scoping (omit-when-0 -> a leaf channel push is byte-identical)
        if (p.enc) j.lit(",\"enc\":true");                        // §chan-crypt CL2a: the post arrived SEALED under the team content key and we OPENED it; omitted (=false) for a plaintext post, so every existing stream is byte-identical. Same shape as msg_recv's `enc` above.
        j.lit(",\"body\":");           j.str(reinterpret_cast<const char*>(p.body), body_n);
    } else if (p.kind == PushKind::team_channel_no_key) {  // §chan-crypt CL2a: a CRYPTED team post we cannot read -> the app prompts "ask a teammate for the key". NO body and NO seq: nothing was inboxed and there is no plaintext to show.
        j.lit(",\"origin\":");         j.u32(p.origin);
        j.lit(",\"layer_id\":");       j.u32(p.layer_id);
        j.lit(",\"channel_id\":");     j.u32(p.channel_id);
        j.lit(",\"channel_msg_id\":"); j.u32(p.channel_msg_id);
        if (p.team_id) { j.lit(",\"team_id\":"); key_hex32(j, p.team_id); }
    } else if (p.kind == PushKind::hash_resolved) {
        const uint32_t hash = static_cast<uint32_t>(p.body[0]) | (static_cast<uint32_t>(p.body[1]) << 8)
                            | (static_cast<uint32_t>(p.body[2]) << 16) | (static_cast<uint32_t>(p.body[3]) << 24);
        j.lit(",\"node\":"); j.u32(p.origin);          // 0 = unresolved / timeout
        j.lit(",\"auth\":"); j.u32(p.dst);
        j.lit(",\"hash\":"); j.u32(hash);
    } else if (p.kind == PushKind::peer_key_cached) {      // E2E §7: a recipient key arrived -> the app can resend encrypted
        j.lit(",\"hash\":");   j.u32(p.sender_hash);
        // ★★ §AB2: `conf` is the AUTHORITY (spec §2.2) — the app gates its "send encrypted" affordance on
        // `conf >= authoritative`, never on the mere presence of a key. `pinned` is KEPT as the DERIVED duplicate
        // (`conf == "pinned"`) rather than removed: it is a documented, possibly-persisted contract field and breaking
        // it is not worth the tidiness. It is no longer a LITERAL — it used to read `false` unconditionally, so a QR-pinned
        // peer's push claimed it was not pinned.
        j.lit(",\"conf\":\""); j.lit(peerkeyconf_name(static_cast<Node::PeerKeyConf>(p.peer_conf))); j.ch('"');
        j.lit(",\"pinned\":");
        j.lit(p.peer_conf == static_cast<uint8_t>(Node::PeerKeyConf::pinned) ? "true" : "false");
        if (body_n) { j.lit(",\"name\":"); j.str(reinterpret_cast<const char*>(p.body), body_n); }   // §S6: the peer's cached name (copied at cache time; omit-when-unknown)
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
    } else if (p.kind == PushKind::mobile_reg) {   // §S2: registration change — the mobile's connectivity chip
        j.lit(",\"home\":");  j.u32(p.origin);
        j.lit(",\"local\":"); j.u32(p.dst);
        if (p.relayed) {                            // registered -> the roam detail; home_layer/epoch omitted on a home-loss
            j.lit(",\"home_layer\":"); j.u32(p.layer_id);
            j.lit(",\"epoch\":");      j.u32(p.ctr);
        }
        j.lit(",\"registered\":"); j.lit(p.relayed ? "true" : "false");
    } else if (p.kind == PushKind::team_reg) {     // §S2: team-DAD id adopted/re-picked
        j.lit(",\"team\":");  key_hex32(j, p.team_id);
        j.lit(",\"local\":"); j.u32(p.dst);
    } else if (p.kind == PushKind::team_key_received) {   // §team-ch-key T-K3: the team CONTENT key arrived by sealed grant (ALREADY adopted — the app just relabels its lock state)
        j.lit(",\"team\":");   key_hex32(j, p.team_id);   // the granted team id, hex8 — SAME spelling as team_reg above (U3)
        j.lit(",\"hash\":");   j.u32(p.sender_hash);      // the GRANTER's stable key_hash32 (the sealed-sender identity), same field name as peer_key_cached
        j.lit(",\"origin\":"); j.u32(p.origin);           // the granter's node id on the receiving plane (diagnostic; `hash` is the durable identity)
        if (body_n) { j.lit(",\"name\":"); j.str(reinterpret_cast<const char*>(p.body), body_n); }   // the granter's optional team label — OMIT-when-absent (this surface emits no JSON nulls) and NOT persisted on the node
        // ⚠ THE KEY ITSELF IS NOT HERE AND MUST NEVER BE. This push is unsolicited; `team exportkey` is the ONE
        // disclosure verb (T-K1b), and `team_ch_key` on ready/cfg is the boolean the app's indicator reads.
    } else if (p.kind == PushKind::join_adopted) {  // a DAD/join adopt landed -> the app refreshes ready.id (staleness fix)
        j.lit(",\"id\":");    j.u32(p.dst);         // the adopted node_id
        j.lit(",\"layer\":"); j.u32(p.layer_id);    // _cfg.leaf_id (the wire leaf nibble)
        j.lit(",\"epoch\":"); j.u32(p.ctr);         // _claim_epoch
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
// 32 raw key bytes -> 64 LOWER-CASE hex digits, UNQUOTED (the caller owns the quotes). ONE definition for every
// 32-byte key blob on this surface (U1): ready's `pubkey` and team_key_export's `tkpub`/`tkpriv`. Lower-case + no
// `0x` is load-bearing, not cosmetic — it is exactly what mrfw::parse_hex32 accepts back (§team-ch-key T-K1b).
static void hex32_digits(JsonBuf& j, const uint8_t* p) {
    static const char H[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) { j.ch(H[p[i] >> 4]); j.ch(H[p[i] & 0xF]); }
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
                   uint8_t duty_pct, uint32_t duty_avail_ms, MobileReadyFields mob) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"ready\",\"id\":"); j.u32(id);
    j.lit(",\"key\":"); key_hex32(j, key);
    if (ed_pub) {                                                          // §4: the full pubkey (64 hex) for the QR `p` — key_hash32 alone can't seal
        j.lit(",\"pubkey\":\""); hex32_digits(j, ed_pub); j.ch('"');        // T-K1b: the inline loop became the shared hex32_digits (U1) — byte-identical, pinned by this file's existing `pubkey` test
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
    // §S1: mobile/team state, ALL omit-when-inactive -> a static, teamless node's ready stays byte-identical.
    if (mob.is_mobile) {
        j.lit(",\"mobile\":true");
        j.lit(",\"mobile_registered\":"); j.lit(mob.registered ? "true" : "false");
        j.lit(",\"mobile_home\":");  j.u32(mob.home);     // 0 = unregistered
        j.lit(",\"mobile_local\":"); j.u32(mob.local);
        if (mob.registered) { j.lit(",\"mobile_home_layer\":"); j.u32(mob.home_layer); }
    }
    if (mob.hosting)    { j.lit(",\"hosting\":"); j.u32(mob.hosting); }        // static host: mobiles registered to us
    if (mob.team_id)    { j.lit(",\"team\":"); key_hex32(j, mob.team_id); }    // key_hex32 style (hex string, like key)
    if (mob.team_local) { j.lit(",\"team_local\":"); j.u32(mob.team_local); }  // our OWN id on the team overlay
    // §team-ch-key (T-K1b): the CONTENT-key LOCK STATE — the field the app's per-team indicator reads, so it never
    // calls `team exportkey` merely to test for presence. Gated on team_id (the same condition as `team` above), so a
    // static/teamless node's ready stays byte-identical; EXPLICIT true/false inside the block, so "absent" is never
    // ambiguous with "false" for a node that IS in a team.
    // ★★ THE KEY ITSELF MUST NEVER APPEAR HERE. `ready` is UNSOLICITED and fires on every connect; team_ch_priv is
    // disclosed ONLY in answer to the explicit `team exportkey` verb. The `pubkey` field above is the precedent for a
    // PUBLIC key and is deliberately NOT the model. Pinned by a test that asserts ready carries no tkpub/tkpriv/key
    // bytes — a boolean is the entire budget of this line.
    if (mob.team_id) { j.lit(",\"team_ch_key\":"); j.lit(mob.team_ch_key ? "true" : "false"); }
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
                      uint32_t sender_hash, uint64_t rx_ms, const char* body, size_t body_len, bool enc, uint8_t type, uint8_t origin_layer) {
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
    if (origin_layer) { j.lit(",\"origin_layer\":"); j.u32(origin_layer); }   // §GapA-durable: the XL sender's layer; omitted (=0) for same-layer
    if (enc) j.lit(",\"enc\":true");                  // §8b: sealed-delivery flag; omitted (=false) for plaintext
    j.lit(",\"body\":");        j.str(body, body_len);
    j.ch('}');
    return j.finish();
}
size_t write_inbox_channel(char* buf, size_t cap, uint32_t seq, uint8_t origin, uint8_t layer_id, uint8_t channel_id,
                           uint32_t channel_msg_id, uint64_t rx_ms, const char* body, size_t body_len, uint32_t team_id) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"inbox_channel\",\"seq\":"); j.u32(seq);
    j.lit(",\"origin\":");         j.u32(origin);
    j.lit(",\"layer_id\":");       j.u32(layer_id);   // §2/Q13: which layer this channel message arrived on
    j.lit(",\"channel_id\":");     j.u32(channel_id);
    j.lit(",\"channel_msg_id\":"); j.u32(channel_msg_id);
    j.lit(",\"rx_ms\":");          j.i64(static_cast<int64_t>(rx_ms));
    if (team_id) { j.lit(",\"team_id\":"); key_hex32(j, team_id); }   // §S5: durable team scoping (omit-when-0)
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

// ★★ §AB3 — one address-book row. See console_json.h for the field contract; the row itself is Node::PeerBookRow, the
// generated view's own carrier (U2: one row definition, no console-side copy to keep in step).
size_t write_peer_row(char* buf, size_t cap, const Node::PeerBookRow& r) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"peer\",\"hash\":"); j.u32(r.hash);
    j.lit(",\"conf\":\"");      j.lit(peerkeyconf_name(r.conf)); j.ch('"');   // the LEVEL (§2.2) — one spelling, -Wswitch-guarded
    j.lit(",\"confirmed\":");   j.lit(r.peer_confirmed ? "true" : "false");
    if (r.name_len)   { j.lit(",\"name\":");       j.str(r.name, r.name_len); }
    if (r.static_id)  { j.lit(",\"static_id\":");  j.u32(r.static_id); }
    if (r.team_id)    { j.lit(",\"team_id\":");    j.u32(r.team_id); }
    if (r.team_alias_dropped) { j.lit(",\"team_alias\":"); j.u32(r.team_alias_dropped); }   // never silently drop a loser
    // ★★ §AB4: the retained position. ALL FOUR fields ride together or none do — `loc_age_s` is MANDATORY beside a
    // position (a position without an age is rendered as current) and `loc_src` is MANDATORY beside it too (a
    // group-anchored claim must never be presented as a pairwise one). Absence is the NORMAL case, not an error.
    if (r.has_location) {
        j.lit(",\"lat\":");       j.i64(r.lat_e7);
        j.lit(",\"lon\":");       j.i64(r.lon_e7);
        j.lit(",\"loc_age_s\":"); j.u32(r.loc_age_s);
        j.lit(",\"loc_src\":\""); j.lit(peerlocsrc_name(r.loc_src)); j.ch('"');
    }
    if (!r.has_key)   j.lit(",\"aged\":true");     // the cached key is past its TTL -> UNUSABLE (conf already reads "overheard")
    j.ch('}');
    return j.finish();
}
size_t write_peers_end(char* buf, size_t cap, uint32_t count) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"peers_end\",\"count\":"); j.u32(count);
    j.ch('}');
    return j.finish();
}
// C2 refusal twin (write_peers_err's shape mirrors write_mobile_err / write_team_key_err — U3). One reason today:
// "console_only", for `peers all` over a companion transport. Naming the remedy is the point: the ≤16-row book IS
// available over BLE as plain `peers`; only the up-to-256 diagnostic list is console-bound (§2.6(a)).
size_t write_peers_err(char* buf, size_t cap, const char* reason) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"peers_err\",\"reason\":\""); j.lit(reason); j.ch('"');
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
    j.lit(",\"team_hop_cap\":"); j.u32(c.team_hop_cap);   // §team-parity T3: the team plane's RREQ/cascade radius, beside its static twin (round-trips `cfg set team_hop_cap`). ⚠ it is NOT yet the team DV cap — see node_beacon.cpp:884
    j.lit(",\"leaf_id\":");    j.u32(c.leaf_id);
    j.lit(",\"gateway\":");    j.lit(c.is_gateway ? "true" : "false");
    j.lit(",\"mobile\":");     j.lit(c.is_mobile ? "true" : "false");
    j.lit(",\"mobile_autoregister\":"); j.lit(c.mobile_autoregister ? "true" : "false");   // §S1: always present (cfg is the explicit dump) — round-trips `cfg set mobile_autoregister`
    j.lit(",\"team_id\":");    key_hex32(j, c.team_id);   // §S1: hex string; "00000000" when unset (explicit, unlike ready's omit)
    j.lit(",\"team_channel_crypt\":"); j.lit(c.team_channel_crypt ? "true" : "false");   // §chan-crypt CL2a (T-K2 §2.5): SEAL a `-t` post by default when a key is held. Pairs with team_ch_key below — key+crypt = encrypted, key+!crypt = opted out, !key = cannot encrypt. Round-trips `cfg set team_channel_crypt`.
    j.lit(",\"team_ch_key\":"); j.lit(x.team_ch_key ? "true" : "false");   // §team-ch-key (T-K1b): the CONTENT-key lock state — the JSON twin of dump_cfg's `team_ch_key=0|1`. ALWAYS present (cfg is the explicit dump, like team_id above). BOOLEAN ONLY — the pair is a secret; `team exportkey` is its one disclosure.
    j.lit(",\"ble_mode\":");   j.str(x.ble_mode, std::strlen(x.ble_mode));
    j.lit(",\"ble_period\":"); j.u32(x.ble_period);
    j.lit(",\"ble_pin\":");    j.u32(x.ble_pin);
    j.lit(",\"lat_e7\":");     j.i64(x.lat_e7);   // signed; degrees×1e7, 0 = unset
    j.lit(",\"lon_e7\":");     j.i64(x.lon_e7);
    write_layers_array(j, c);
    j.ch('}');
    return j.finish();
}

// ---- §S3: `mobile status` + `mobile gateways` as JSON (PODs in; no node.h dep; src/-only call-site swap) ----
size_t write_mobile_status(char* buf, size_t cap, const MobileStatusFields& m) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"mobile_status\",\"mobile\":true,\"registered\":"); j.lit(m.registered ? "true" : "false");
    j.lit(",\"home\":");  j.u32(m.home);              // 0 when unregistered
    j.lit(",\"local\":"); j.u32(m.local);
    j.lit(",\"epoch\":"); j.u32(m.epoch);
    if (m.registered) { j.lit(",\"home_layer\":"); j.u32(m.home_layer); }
    j.lit(",\"autoregister\":"); j.lit(m.autoregister ? "true" : "false");
    j.lit(",\"layer\":");    j.u32(m.layer);          // the live PHY layer
    j.lit(",\"freq_khz\":"); j.u32(m.freq_khz);       // integer kHz (no float on the wire)
    j.lit(",\"sf\":");       j.u32(m.sf);
    j.lit(",\"bw_hz\":");    j.u32(m.bw_hz);
    j.lit(",\"nets\":");     j.u32(m.nets);           // learned-networks count (rows come from `mobile gateways`)
    j.ch('}');
    return j.finish();
}
size_t write_mobile_err(char* buf, size_t cap, const char* reason) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"mobile_err\",\"reason\":\""); j.lit(reason); j.ch('"'); j.ch('}');
    return j.finish();
}
size_t write_mobile_gw(char* buf, size_t cap, uint8_t gw, uint8_t leaf) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"mobile_gw\",\"gw\":"); j.u32(gw);
    j.lit(",\"leaf\":"); j.u32(leaf);
    j.ch('}');
    return j.finish();
}
size_t write_mobile_net(char* buf, size_t cap, uint8_t layer, const char* name, size_t name_len,
                        uint32_t freq_khz, uint8_t sf, uint32_t bw_hz) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"mobile_net\",\"layer\":"); j.u32(layer);
    j.lit(",\"name\":");     j.str(name ? name : "", name ? name_len : 0);
    j.lit(",\"freq_khz\":"); j.u32(freq_khz);         // already integer kHz in the LayerRecord
    j.lit(",\"sf\":");       j.u32(sf);
    j.lit(",\"bw_hz\":");    j.u32(bw_hz);
    j.ch('}');
    return j.finish();
}
size_t write_mobile_gw_end(char* buf, size_t cap, uint8_t gws, uint8_t nets) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"mobile_gw_end\",\"gws\":"); j.u32(gws);
    j.lit(",\"nets\":"); j.u32(nets);
    j.ch('}');
    return j.finish();
}
// §team-ch-key (T-K1b): `team exportkey` — the ONE disclosure of the team CONTENT keypair (contract "node → app:
// export the team channel keypair"). See console_json.h for the clamped-verbatim + lower-case-hex contract; the
// keyless answer is write_team_key_err below, never this event with null fields.
size_t write_team_key_export(char* buf, size_t cap, uint32_t team_id, const uint8_t pub[32], const uint8_t priv[32]) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"team_key_export\",\"team_id\":"); j.u32(team_id);   // DECIMAL u32 (the contract's own example) — NOT key_hex32
    j.lit(",\"tkpub\":\"");  hex32_digits(j, pub);  j.ch('"');
    j.lit(",\"tkpriv\":\""); hex32_digits(j, priv); j.ch('"');
    j.ch('}');
    return j.finish();
}
// The refused answer (mobile_err's shape verbatim — U3): "no_team" | "no_key". A DISTINCT `ev` so a null-blind
// consumer can never mistake a refusal for a payload and write `null`/32 zero bytes into a team QR.
size_t write_team_key_err(char* buf, size_t cap, const char* reason) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"team_key_err\",\"reason\":\""); j.lit(reason); j.ch('"'); j.ch('}');
    return j.finish();
}
// §team-ch-key (T-K3): the `team grantkey` ACCEPTED answer. See console_json.h for the field contract; `parked` is
// stated rather than left implicit in `ctr == 0` so the app never has to infer a state from a sentinel.
size_t write_team_key_grant(char* buf, size_t cap, uint32_t target_hash, uint16_t ctr) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"team_key_grant\",\"hash\":"); j.u32(target_hash);
    j.lit(",\"ctr\":");    j.u32(ctr);
    j.lit(",\"parked\":"); j.lit(ctr ? "false" : "true");
    j.ch('}');
    return j.finish();
}
// §S6: `nameof` answer — decimal-u32 hash + the cached name (omitted when unknown, same rule as peer_key_cached).
// ★ §AB3: + the view's static_id/team_id, omit-when-0. `nameof` now reads the GENERATED view rather than
// peer_name_find alone, so it and `hashof` and `peers` cannot answer one identity three ways (spec §2.5).
size_t write_peer_name(char* buf, size_t cap, uint32_t hash, const char* name, size_t name_len,
                       uint8_t static_id, uint8_t team_id) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"peer_name\",\"hash\":"); j.u32(hash);
    if (name && name_len) { j.lit(",\"name\":"); j.str(name, name_len); }
    if (static_id) { j.lit(",\"static_id\":"); j.u32(static_id); }
    if (team_id)   { j.lit(",\"team_id\":");   j.u32(team_id); }
    j.ch('}');
    return j.finish();
}
// ★ §AB2 (spec §2.3 + the §2.6(b) ruling): the `peername` SYNCHRONOUS ACK. Not a push — the verb is operator-initiated,
// purely local and its result is known immediately, so there is nothing asynchronous to notify; a push would also have
// meant a new PushKind, which the sim bridges on its raw uint8_t with static_asserts twinned in two files.
// `name` is ECHOED (j.str escapes + UTF-8-sanitises it) so the app can confirm what the node actually stored rather
// than assume its own string round-tripped. Field ORDER mirrors the `peerkey_set` ack (hash first, decimal u32) — that
// ack is still snprintf'd inline in src/firmware_commands.cpp rather than written here; folding it in is a cleanup
// slice, not this one (C1), and it is noted so it can be found.
size_t write_peer_name_set(char* buf, size_t cap, uint32_t hash, const char* name, size_t name_len) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"peer_name_set\",\"hash\":"); j.u32(hash);
    j.lit(",\"name\":"); j.str(name, name_len);
    j.ch('}');
    return j.finish();
}
// The refusal twin, same shape as write_peerkey_err / write_team_key_err (U3). `reason` ∈ "unknown_hash" (no cached row
// for that hash — remedy: `reqpubkey`) | "too_long" (> protocol::peer_name_max — remedy: shorten) | "bad_args" (a
// malformed hash, an unquoted/empty/unterminated name). The three have DIFFERENT remedies, which is why they are three
// reasons and not one `bad_args` (C2: a refusal must name the way out).
size_t write_peer_name_err(char* buf, size_t cap, const char* reason) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"peer_name_err\",\"reason\":\""); j.lit(reason); j.ch('"'); j.ch('}');
    return j.finish();
}

}  // namespace meshroute::console
