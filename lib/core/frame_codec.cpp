// MeshRoute — frame_codec.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §10 cmd-nibble wire codecs (codec track C1–C6, COMPLETE). Implemented: BCN
// (0x0), RTS (0x1), CTS (0x2), DATA (0x3), ACK (0x4), NACK (0x5), Q (0x6),
// H (0x7), F (0x8), J (0x9) — every §10.1 primary command code.
//
// Wire authority: byte positions = ROADMAP §10.3. The C++ wire DIVERGES from the
// Lua tag-byte wire by design (cmd-nibble, shorter frames) — NOT byte-for-byte.
// Field *meaning* = the Lua pack_*/parse_* code (dv_dual_sf.lua); genuine Lua
// bugs are fixed in both sides.

#include "frame_codec.h"

#include "wire.h"
#include "protocol_constants.h"   // bcn_ext_type_channel_digest + channel_dirty_max_per_bcn (channel-digest TLV)

namespace MESHROUTE_NS {

// -----------------------------------------------------------------------------
// BCN — cmd=0x0, variable length (ROADMAP §10.3). See frame_codec.h for layout.
// -----------------------------------------------------------------------------
size_t pack_beacon(const beacon_in& in, std::span<uint8_t> out) {
    const bool has_schedule    = !in.schedule.empty();
    const bool has_seen_bitmap = !in.seen_bitmap.empty();
    const bool has_ext         = !in.ext.empty();
    if (in.schedule.size() > 15) return 0;   // layer_count is a 4-bit nibble
    if (in.entries.size()  > 63) return 0;    // n_entries is a 6-bit split field
    if (in.ext.size()     > 255) return 0;    // ext_len is one byte
    if (has_seen_bitmap && in.seen_bitmap.size() != 32) return 0;   // seen-bitmap is exactly 32 B
    const uint8_t n_entries = static_cast<uint8_t>(in.entries.size());

    wire::Writer w(out);
    w.u8(wire::cmd_byte(wire::Cmd::B, static_cast<uint8_t>(in.leaf_id & 0x0F)));
    w.u8(in.src);
    w.u8(static_cast<uint8_t>((has_schedule    ? 0x80 : 0) | (in.self_gateway ? 0x40 : 0) |
                              (in.is_mobile     ? 0x20 : 0) | (has_seen_bitmap ? 0x10 : 0) |
                              (has_ext          ? 0x08 : 0) | (n_entries & 0x07)));
    w.u8(static_cast<uint8_t>((((n_entries >> 3) & 0x07) << 5) | (in.heard_set_complete ? 0x10 : 0x00) | (protocol::wire_version & 0x0F)));   // n_entries_hi | complete(b4) | wire_version (b3..0; §7c)
    w.u32_le(in.key_hash32);
    // R6.1 leaf-config header (+6 B, FIXED, pre-schedule — never truncated): lineage_id · config_epoch · config_hash (u16×3).
    w.u16_le(in.lineage_id);
    w.u16_le(in.config_epoch);
    w.u16_le(in.config_hash);

    if (has_schedule) {
        w.u8(static_cast<uint8_t>(((in.gateway_spread_nibble & 0x0F) << 4) |
                                  (static_cast<uint8_t>(in.schedule.size()) & 0x0F)));
        for (const schedule_record& s : in.schedule) {
            w.u8(static_cast<uint8_t>(((s.layer_id & 0x0F) << 4) |
                                      (((s.routing_sf - 5) & 0x07) << 1) |
                                      (s.period_unit_5s ? 0x01 : 0x00)));
            w.u8(s.duration_100ms);
            w.u8(s.offset_100ms);     // runtime re-stamps this byte at TX; codec packs as given
            w.u8(s.period_units);
        }
    }
    for (const beacon_entry& e : in.entries) {
        w.u8(e.dest);
        w.u8(e.next);
        w.u8(static_cast<uint8_t>(((e.score_bucket & 0x0F) << 4) | (e.degraded ? 0x08 : 0x00) | (e.is_gateway ? 0x01 : 0x00)));
        w.u8(e.hops);
    }
    if (has_seen_bitmap) {
        for (uint8_t b : in.seen_bitmap) w.u8(b);   // opaque 32-B copy (size validated above)
    }
    if (has_ext) {
        w.u8(static_cast<uint8_t>(in.ext.size()));
        for (uint8_t b : in.ext) w.u8(b);                       // opaque payload (TLVs decoded upstream)
    }
    return w.ok() ? w.size() : 0;
}

uint8_t beacon_max_entries(size_t frame_cap, size_t sched_bytes, size_t bitmap_bytes, size_t ext_block_bytes) {
    const size_t overhead = 8 + BCN_LEAF_HEADER_LEN + sched_bytes + bitmap_bytes + ext_block_bytes;   // 8-B header + 10-B leaf header + variable blocks
    if (overhead >= frame_cap) return 0;                                        // no room for any 4-B entry
    size_t n = (frame_cap - overhead) / 4;
    return static_cast<uint8_t>(n > 63 ? 63 : n);                               // 6-bit n_entries field
}

std::optional<beacon_out> parse_beacon(std::span<const uint8_t> frame) {
    if (frame.size() < 8 + BCN_LEAF_HEADER_LEN) return std::nullopt;   // R6.1 FLAG-DAY: the 10-B leaf header is mandatory
    if (wire::cmd_of(frame[0]) != wire::Cmd::B) return std::nullopt;

    beacon_out o{};
    o.leaf_id         = wire::flags_of(frame[0]);
    o.src             = frame[1];
    const uint8_t b2  = frame[2];
    const uint8_t b3  = frame[3];
    o.has_schedule    = (b2 & 0x80) != 0;
    o.self_gateway    = (b2 & 0x40) != 0;
    o.is_mobile       = (b2 & 0x20) != 0;
    o.has_seen_bitmap = (b2 & 0x10) != 0;
    o.has_ext         = (b2 & 0x08) != 0;
    o.n_entries       = static_cast<uint8_t>((b2 & 0x07) | (((b3 >> 5) & 0x07) << 3));
    o.wire_version    = static_cast<uint8_t>(b3 & 0x0F);   // §7c: cross-version handshake (byte-3 low nibble, fixed offset)
    o.heard_set_complete = (b3 & 0x10) != 0;               // byte-3 b4 ONLY — does not touch wire_version / n_entries_hi
    { wire::Reader r(frame.subspan(4, 4)); o.key_hash32 = r.u32_le(); }
    // R6.1 leaf-config header (bytes 8..13, FIXED, always present): lineage_id · config_epoch · config_hash (u16×3)
    { wire::Reader r(frame.subspan(8, BCN_LEAF_HEADER_LEN));
      o.lineage_id = r.u16_le(); o.config_epoch = r.u16_le(); o.config_hash = r.u16_le(); }

    size_t pos = 8 + BCN_LEAF_HEADER_LEN;
    if (o.has_schedule) {
        if (pos + 1 > frame.size()) return std::nullopt;
        const uint8_t lc = frame[pos];
        o.gateway_spread_nibble = static_cast<uint8_t>((lc >> 4) & 0x0F);
        o.schedule_count        = static_cast<uint8_t>(lc & 0x0F);
        o.schedule_off          = pos + 1;
        pos += 1 + static_cast<size_t>(o.schedule_count) * 4;
        if (pos > frame.size()) return std::nullopt;
    } else {
        o.schedule_off = pos;   // no records; offset is harmless
    }

    o.entries_off = pos;
    pos += static_cast<size_t>(o.n_entries) * 4;
    if (pos > frame.size()) return std::nullopt;

    if (o.has_seen_bitmap) {
        o.seen_off = pos;
        pos += 32;
        if (pos > frame.size()) return std::nullopt;
    }
    if (o.has_ext) {
        if (pos + 1 > frame.size()) return std::nullopt;
        o.ext_off = pos;
        o.ext_len = frame[pos];
        pos += 1 + o.ext_len;
        if (pos > frame.size()) return std::nullopt;
    }
    // Reject trailing bytes. NB: STRICTER than the Lua parse_beacon, which only
    // lower-bounds (`#frame < X`) and silently ignores trailing bytes — not
    // Lua-identical. Harmless (pack emits exact-length frames) and a useful guard
    // on this variable-length frame; keep it strict.
    if (pos != frame.size()) return std::nullopt;
    o.frame_len = pos;
    return o;
}

std::optional<beacon_entry> parse_beacon_entry(std::span<const uint8_t> frame,
                                               const beacon_out& bcn, uint8_t i) {
    if (i >= bcn.n_entries) return std::nullopt;
    const size_t off = bcn.entries_off + static_cast<size_t>(i) * 4;
    if (off + 4 > frame.size()) return std::nullopt;
    beacon_entry e{};
    e.dest         = frame[off];
    e.next         = frame[off + 1];
    e.score_bucket = static_cast<uint8_t>((frame[off + 2] >> 4) & 0x0F);
    e.is_gateway   = (frame[off + 2] & 0x01) != 0;
    e.degraded     = (frame[off + 2] & 0x08) != 0;   // byte-2 b3 (asymmetric-link-aware routing); rsv b2..1 still ignored
    e.hops         = frame[off + 3];
    return e;
}

std::optional<schedule_record> parse_beacon_schedule(std::span<const uint8_t> frame,
                                                     const beacon_out& bcn, uint8_t i) {
    if (i >= bcn.schedule_count) return std::nullopt;
    const size_t off = bcn.schedule_off + static_cast<size_t>(i) * 4;
    if (off + 4 > frame.size()) return std::nullopt;
    schedule_record s{};
    const uint8_t b0 = frame[off];
    s.layer_id       = static_cast<uint8_t>((b0 >> 4) & 0x0F);
    s.routing_sf     = static_cast<uint8_t>(((b0 >> 1) & 0x07) + 5);
    s.period_unit_5s = (b0 & 0x01) != 0;
    s.duration_100ms = frame[off + 1];
    s.offset_100ms   = frame[off + 2];
    s.period_units   = frame[off + 3];
    return s;
}

std::span<const uint8_t> beacon_seen_bitmap(std::span<const uint8_t> frame, const beacon_out& bcn) {
    if (!bcn.has_seen_bitmap) return {};
    if (bcn.seen_off + 32 > frame.size()) return {};
    return frame.subspan(bcn.seen_off, 32);
}

std::span<const uint8_t> beacon_ext(std::span<const uint8_t> frame, const beacon_out& bcn) {
    if (!bcn.has_ext) return {};
    if (bcn.ext_off + 1 + bcn.ext_len > frame.size()) return {};
    return frame.subspan(bcn.ext_off + 1, bcn.ext_len);
}

// BCN channel-digest ext-TLV (dv:1426 build / 1965 parse): [type<<4 | body_len][count][N × id (4B BE)].
size_t pack_channel_digest_tlv(const uint32_t* ids, uint8_t count, std::span<uint8_t> out) {
    if (count > protocol::channel_dirty_max_per_bcn) count = protocol::channel_dirty_max_per_bcn;  // 4-bit len caps body<=13
    const uint8_t body_len = static_cast<uint8_t>(1 + 4 * count);
    wire::Writer w(out);
    w.u8(static_cast<uint8_t>((protocol::bcn_ext_type_channel_digest << 4) | (body_len & 0x0f)));
    w.u8(count);
    for (uint8_t i = 0; i < count; ++i) w.u32_be(ids[i]);
    return w.ok() ? w.size() : 0;
}
// Scan the ext block for the type-3 TLV; extract up to `max` ids. Skips other TLV types (forward-compat).
uint8_t parse_channel_digest_tlv(std::span<const uint8_t> ext, uint32_t* ids_out, uint8_t max) {
    size_t o = 0;
    while (o < ext.size()) {
        const uint8_t type = static_cast<uint8_t>(ext[o] >> 4);
        const uint8_t blen = static_cast<uint8_t>(ext[o] & 0x0f);
        if (o + 1 + blen > ext.size()) break;                       // truncated TLV -> stop
        if (type == protocol::bcn_ext_type_channel_digest) {
            const std::span<const uint8_t> body = ext.subspan(o + 1, blen);
            if (body.empty()) return 0;
            const uint8_t count = body[0];
            uint8_t n = 0;
            for (uint8_t i = 0; i < count && n < max; ++i) {
                const size_t off = 1 + 4 * static_cast<size_t>(i);
                if (off + 4 > body.size()) break;                   // body too short for the claimed count
                ids_out[n++] = (uint32_t(body[off]) << 24) | (uint32_t(body[off + 1]) << 16)
                             | (uint32_t(body[off + 2]) << 8) | uint32_t(body[off + 3]);
            }
            return n;
        }
        o += 1u + blen;                                             // skip other TLV types
    }
    return 0;
}

// BCN gateway-layer ext-TLV (type 4) — the Lua split-list (dv:1513 build / dv:1977 parse), byte-for-byte.
size_t pack_gateway_layer_tlv(const GwLayerEntry* e, uint8_t n, std::span<uint8_t> out) {
    if (n == 0) return 0;                                            // empty -> emit NO TLV (the s18 keystone; caller relies on this)
    if (n > protocol::bridged_layers_max_per_tlv) n = protocol::bridged_layers_max_per_tlv;
    const uint8_t nibbles  = static_cast<uint8_t>((n + 1) / 2);      // ceil(N/2)
    const uint8_t body_len = static_cast<uint8_t>(n + nibbles);
    if (body_len > 15) return 0;                                     // 4-bit len cap (n<=9 -> <=14; defensive)
    wire::Writer w(out);
    w.u8(static_cast<uint8_t>((protocol::bcn_ext_type_gateway_layer << 4) | (body_len & 0x0f)));
    for (uint8_t i = 0; i < n; ++i) w.u8(e[i].gw_id);
    for (uint8_t bi = 0; bi < nibbles; ++bi) {                       // entry 2*bi -> LOW nibble; entry 2*bi+1 -> HIGH nibble
        uint8_t v = static_cast<uint8_t>(e[2 * bi].dest_leaf & 0x0f);
        if (static_cast<uint8_t>(2 * bi + 1) < n)
            v = static_cast<uint8_t>(v | ((e[2 * bi + 1].dest_leaf & 0x0f) << 4));
        w.u8(v);
    }
    return w.ok() ? w.size() : 0;
}
// Scan the ext block for the type-4 TLV; N is inferred from body_len (no count byte). A duplicate gw_id discards
// the WHOLE TLV (return 0), mirroring the Lua. Skips other TLV types (forward-compat).
uint8_t parse_gateway_layer_tlv(std::span<const uint8_t> ext, GwLayerEntry* out, uint8_t max) {
    size_t o = 0;
    while (o < ext.size()) {
        const uint8_t type = static_cast<uint8_t>(ext[o] >> 4);
        const uint8_t blen = static_cast<uint8_t>(ext[o] & 0x0f);
        if (o + 1 + blen > ext.size()) break;                       // truncated TLV -> stop
        if (type == protocol::bcn_ext_type_gateway_layer) {
            const std::span<const uint8_t> body = ext.subspan(o + 1, blen);
            const uint8_t N = static_cast<uint8_t>((2u * static_cast<unsigned>(blen)) / 3u);   // Lua dv:1981
            if (N == 0) return 0;
            if (static_cast<size_t>(N) + (N + 1) / 2 != blen) return 0;                        // body_len must == N + ceil(N/2)
            uint8_t seen[32] = {};                                   // gw_id (0..255) dedup bitset — each gw_id at most once (Lua dv:1985)
            uint8_t cnt = 0;
            for (uint8_t i = 0; i < N; ++i) {
                const uint8_t gw_id = body[i];
                const uint8_t nb    = body[static_cast<size_t>(N) + i / 2];
                const uint8_t leaf  = (i % 2 == 0) ? static_cast<uint8_t>(nb & 0x0f)
                                                   : static_cast<uint8_t>((nb >> 4) & 0x0f);
                if (seen[gw_id >> 3] & (1u << (gw_id & 7))) return 0;                          // duplicate gw_id -> discard whole TLV (Lua dv:1997)
                seen[gw_id >> 3] = static_cast<uint8_t>(seen[gw_id >> 3] | (1u << (gw_id & 7)));
                if (cnt < max) out[cnt++] = GwLayerEntry{ gw_id, leaf };
            }
            return cnt;
        }
        o += 1u + blen;                                             // skip other TLV types
    }
    return 0;
}

// §P4 BCN suspect-nodes ext-TLV (type 1, dv:1413). Wire: [type<<4 | N][N × node_id(1B)]. A SILENT-only advertise set.
// The receiver applies each id as SUSPECT (level 1) — the wire carries NO state byte for this variant.
size_t pack_suspect_nodes_tlv(const uint8_t* ids, uint8_t n, std::span<uint8_t> out) {
    if (n == 0) return 0;                                            // empty -> emit NO TLV
    if (n > protocol::peer_suspect_bcn_max) n = protocol::peer_suspect_bcn_max;   // <=8 (and <=15 for the 4-bit len)
    wire::Writer w(out);
    w.u8(static_cast<uint8_t>((protocol::bcn_ext_type_suspect_nodes << 4) | (n & 0x0f)));   // body_len = N (1B/id)
    for (uint8_t i = 0; i < n; ++i) w.u8(ids[i]);
    return w.ok() ? w.size() : 0;
}
// §P4 BCN liveness-state ext-TLV (type 2, dv:1401). Wire: [type<<4 | 2N][N × (node_id(1B), state(1B & 0x03))].
// Emitted when the advertise set contains a DEAD peer; state 2=SILENT / 3=DEAD. Clamped to 7 entries so 2N<=14<=15
// (the Lua wraps at >=8 — a shared bug we fix here).
size_t pack_liveness_state_tlv(const SuspectEntry* e, uint8_t n, std::span<uint8_t> out) {
    if (n == 0) return 0;
    if (n > protocol::peer_liveness_state_bcn_max) n = protocol::peer_liveness_state_bcn_max;   // <=7 -> body 2N<=14
    const uint8_t body_len = static_cast<uint8_t>(2 * n);
    wire::Writer w(out);
    w.u8(static_cast<uint8_t>((protocol::bcn_ext_type_liveness_state << 4) | (body_len & 0x0f)));
    for (uint8_t i = 0; i < n; ++i) { w.u8(e[i].node_id); w.u8(static_cast<uint8_t>(e[i].state & 0x03)); }
    return w.ok() ? w.size() : 0;
}
// §P4 scan the ext block for a suspect TLV — a beacon carries EITHER type 1 OR type 2. type 1 -> each id at SUSPECT(1);
// type 2 -> [id,state] pairs (rejects odd body, Lua dv:1955). Returns the entry count; skips other TLV types (fwd-compat).
uint8_t parse_suspect_tlv(std::span<const uint8_t> ext, SuspectEntry* out, uint8_t max) {
    size_t o = 0;
    while (o < ext.size()) {
        const uint8_t type = static_cast<uint8_t>(ext[o] >> 4);
        const uint8_t blen = static_cast<uint8_t>(ext[o] & 0x0f);
        if (o + 1 + blen > ext.size()) break;                       // truncated TLV -> stop
        if (type == protocol::bcn_ext_type_suspect_nodes) {
            const std::span<const uint8_t> body = ext.subspan(o + 1, blen);
            uint8_t cnt = 0;
            for (uint8_t i = 0; i < blen && cnt < max; ++i) out[cnt++] = SuspectEntry{ body[i], 1 };   // type-1 -> SUSPECT
            return cnt;
        }
        if (type == protocol::bcn_ext_type_liveness_state) {
            if (blen & 1u) return 0;                                 // odd body -> malformed (Lua dv:1955)
            const std::span<const uint8_t> body = ext.subspan(o + 1, blen);
            uint8_t cnt = 0;
            for (uint8_t i = 0; static_cast<size_t>(i) + 1 < blen && cnt < max; i = static_cast<uint8_t>(i + 2))
                out[cnt++] = SuspectEntry{ body[i], static_cast<uint8_t>(body[i + 1] & 0x03) };
            return cnt;
        }
        o += 1u + blen;                                             // skip other TLV types
    }
    return 0;
}

// -----------------------------------------------------------------------------
// CTS — cmd=0x2, 3 B (ROADMAP §10.3)
// -----------------------------------------------------------------------------
size_t pack_cts(const cts_in& in, std::span<uint8_t> out) {
    if (in.chosen_data_sf < 5 || in.chosen_data_sf > 12) return 0;
    const bool with_pl = in.payload_len != 0;          // NAV: optional 4th byte (sender adds it iff nav_enabled)
    if (out.size() < (with_pl ? 4u : 3u)) return 0;
    wire::Writer w(out);
    const uint8_t sf3 = static_cast<uint8_t>((in.chosen_data_sf - 5) & 0x07);
    // flags in the cmd byte's low nibble: (sf-5)(3) | already_received(1) — mirrors the Lua flags byte.
    w.u8(wire::cmd_byte(wire::Cmd::C, static_cast<uint8_t>((sf3 << 1) | (in.already_received ? 0x01 : 0x00))));
    w.u8(in.tx_id);
    w.u8(in.rx_id);
    if (with_pl) w.u8(in.payload_len);                 // cleared DATA's inner+MAC length (for the overhearer's NAV)
    return w.ok() ? w.size() : 0;
}

std::optional<cts_out> parse_cts(std::span<const uint8_t> frame) {
    if (frame.size() != 3 && frame.size() != 4) return std::nullopt;
    wire::Reader r(frame);
    const uint8_t b0 = r.u8();
    if (wire::cmd_of(b0) != wire::Cmd::C) return std::nullopt;
    const uint8_t tx_id = r.u8();
    const uint8_t rx_id = r.u8();
    const uint8_t payload_len = (frame.size() == 4) ? r.u8() : 0;   // NAV: optional 4th byte (0 => absent)
    if (!r.ok()) return std::nullopt;
    const uint8_t flags = wire::flags_of(b0);          // (sf-5)(3) | already_received(1)
    cts_out o{};
    o.chosen_data_sf   = static_cast<uint8_t>(((flags >> 1) & 0x07) + 5);
    o.already_received = (flags & 0x01) != 0;
    o.tx_id            = tx_id;
    o.rx_id            = rx_id;
    o.payload_len      = payload_len;
    return o;
}

// -----------------------------------------------------------------------------
// ACK — cmd=0x4, 3 B (ROADMAP §10.3)
// -----------------------------------------------------------------------------
size_t pack_ack(const ack_in& in, std::span<uint8_t> out) {
    if (out.size() < 3) return 0;
    wire::Writer w(out);
    // budget_hint SATURATES at 3 (matches Lua pack_ack, dv_dual_sf.lua:2130-2131:
    // `if hint > 3 then hint = 3 end`) — not a wrapping mask. snr_bucket is
    // inherently 0..3 from the mapping, so its & 0x03 matches the Lua's no-op.
    const uint8_t bh = in.budget_hint > 3 ? 3 : in.budget_hint;
    w.u8(wire::cmd_byte(wire::Cmd::K, static_cast<uint8_t>(in.ctr_lo & 0x0F)));
    w.u8(static_cast<uint8_t>((bh << 6) | ((in.snr_bucket & 0x03) << 4) | (in.mobile_to ? 0x02 : 0) | (in.warn ? 0x01 : 0)));  // b1 MOBILE (to is a mobile local-id §mobile Slice 1) · b0 AIRTIME_WARN
    w.u8(in.to);
    return w.ok() ? w.size() : 0;
}

std::optional<ack_out> parse_ack(std::span<const uint8_t> frame) {
    if (frame.size() != 3) return std::nullopt;
    wire::Reader r(frame);
    const uint8_t b0 = r.u8();
    if (wire::cmd_of(b0) != wire::Cmd::K) return std::nullopt;
    const uint8_t b1 = r.u8();
    const uint8_t to = r.u8();
    if (!r.ok()) return std::nullopt;
    ack_out o{};
    o.ctr_lo      = wire::flags_of(b0);
    o.budget_hint = static_cast<uint8_t>((b1 >> 6) & 0x03);
    o.snr_bucket  = static_cast<uint8_t>((b1 >> 4) & 0x03);
    o.to          = to;
    o.warn        = (b1 & 0x01) != 0;   // DM Inc 3 airtime warn (byte1 rsv-nibble bit 0)
    o.mobile_to   = (b1 & 0x02) != 0;   // §mobile Slice 1: MOBILE mark (byte-1 b1) — the `to` is a mobile local-id
    return o;
}

// -----------------------------------------------------------------------------
// RTS — cmd 0x1, 7 B (+2 if M_BROADCAST). byte-5 reading A: flags at bits 5..2.
// -----------------------------------------------------------------------------
size_t pack_rts(const rts_in& in, std::span<uint8_t> out) {
    const bool flood   = (in.rts_flags & RTS_FLAG_FLOOD) != 0;     // FLOOD wins the tail (it also sets M_BROADCAST)
    const bool m_bcast = (in.rts_flags & RTS_FLAG_M_BROADCAST) != 0;
    if (flood && in.flood_bitmap.size() != 32) return 0;          // no-fallback: the 32-B bitmap must be exactly 32 B
    const size_t need = flood ? 43 : (m_bcast ? 9 : 7);
    if (out.size() < need) return 0;
    if (in.addr_len > 1) return 0;                                 // §mobile Slice 1: 0=normal, 1=mobile-next; 2..7 hierarchy-deferred (keep the pack honest)
    wire::Writer w(out);
    w.u8(wire::cmd_byte(wire::Cmd::R, static_cast<uint8_t>(in.leaf_id & 0x0F)));
    w.u8(in.src);
    w.u8(in.next);
    w.u8(static_cast<uint8_t>(((in.ctr_lo & 0x0F) << 4) | ((in.addr_len & 0x07) << 1)));  // ctr_lo hi | addr_len b3..1 (1=mobile-next) | rsv b0
    w.u8(in.dst);                                                  // FLOOD: hop_left rides this slot
    w.u8(static_cast<uint8_t>(((in.sf_index & 0x03) << 6) |        // reading A: sf_index 7..6,
                              ((in.rts_flags & 0x0F) << 2) |       //            rts_flags 5..2,
                              (in.mobile_src ? 0x02 : 0)));        // §mobile: MOBILE b1 (src is a mobile local-id), rsv b0
    w.u8(in.payload_len);                                          // mod-256 enforced by uint8_t
    if (flood) { w.u32_be(in.flood_channel_msg_id); for (uint8_t b : in.flood_bitmap) w.u8(b); }  // 4 B id + 32 B bitmap
    else if (m_bcast) w.u16_be(in.m_payload_id_lo16);
    return w.ok() ? w.size() : 0;
}

std::optional<rts_out> parse_rts(std::span<const uint8_t> frame) {
    if (frame.size() < 7) return std::nullopt;
    wire::Reader r(frame);
    const uint8_t b0 = r.u8();
    if (wire::cmd_of(b0) != wire::Cmd::R) return std::nullopt;
    rts_out o{};
    o.leaf_id     = wire::flags_of(b0);
    o.src         = r.u8();
    o.next        = r.u8();
    const uint8_t b3 = r.u8();
    o.dst         = r.u8();
    const uint8_t b5 = r.u8();
    o.payload_len = r.u8();
    if (!r.ok()) return std::nullopt;
    o.ctr_lo   = static_cast<uint8_t>((b3 >> 4) & 0x0F);
    o.addr_len = static_cast<uint8_t>((b3 >> 1) & 0x07);
    if (o.addr_len > 1) return std::nullopt;                       // §mobile Slice 1: 0=normal, 1=mobile-next; 2..7 hierarchy-deferred -> reject
    o.sf_index  = static_cast<uint8_t>((b5 >> 6) & 0x03);
    o.rts_flags = static_cast<uint8_t>((b5 >> 2) & 0x0F);          // reading A
    o.mobile_src = (b5 & 0x02) != 0;                               // §mobile: MOBILE mark (byte-5 b1) — src is a mobile local-id
    o.m_broadcast = (o.rts_flags & RTS_FLAG_M_BROADCAST) != 0;
    o.flood       = (o.rts_flags & RTS_FLAG_FLOOD) != 0;
    o.m_payload_id_lo16 = 0;
    if (o.flood) {                                                // FLOOD tail REPLACES the id_lo16 tail
        if (frame.size() < 43) return std::nullopt;              // need the full 4-B id + 32-B bitmap
        o.flood_channel_msg_id = r.u32_be();                     // bytes 7-10
        o.flood_bitmap_off     = 11;                             // bytes 11-42 (exposed via rts_flood_bitmap)
    } else if (o.m_broadcast && frame.size() >= 9) {
        o.m_payload_id_lo16 = r.u16_be();
    }
    return o;
}

std::span<const uint8_t> rts_flood_bitmap(std::span<const uint8_t> frame, const rts_out& o) {
    if (!o.flood || o.flood_bitmap_off + 32 > frame.size()) return {};
    return frame.subspan(o.flood_bitmap_off, 32);
}

// -----------------------------------------------------------------------------
// NACK — cmd 0x5, 4 B
// -----------------------------------------------------------------------------
size_t pack_nack(const nack_in& in, std::span<uint8_t> out) {
    if (out.size() < 4) return 0;
    wire::Writer w(out);
    w.u8(wire::cmd_byte(wire::Cmd::N, static_cast<uint8_t>(in.reason & 0x0F)));
    w.u8(static_cast<uint8_t>((in.ctr_lo & 0x0F) << 4));           // ctr_lo hi, rsv lo
    w.u8(in.payload);                                              // reason-specific, packed verbatim
    w.u8(in.to);
    return w.ok() ? w.size() : 0;
}

std::optional<nack_out> parse_nack(std::span<const uint8_t> frame) {
    if (frame.size() != 4) return std::nullopt;
    wire::Reader r(frame);
    const uint8_t b0 = r.u8();
    if (wire::cmd_of(b0) != wire::Cmd::N) return std::nullopt;
    nack_out o{};
    o.reason  = wire::flags_of(b0);
    const uint8_t b1 = r.u8();
    o.ctr_lo  = static_cast<uint8_t>((b1 >> 4) & 0x0F);
    o.payload = r.u8();
    o.to      = r.u8();
    if (!r.ok()) return std::nullopt;
    return o;
}

// -----------------------------------------------------------------------------
// Q — cmd 0x6, 4 B header (+ CHANNEL_PULL body: count + N × 4-B BE channel_msg_id)
// -----------------------------------------------------------------------------
size_t pack_q(const q_in& in, std::span<uint8_t> out) {
    const bool pull   = (in.opcode == q_opcode::channel_pull);
    const bool cfg    = (in.opcode == q_opcode::config_pull);   // R6.2: bytes 4..7 = lineage u16 + epoch u16
    const size_t n = pull ? in.channel_ids.size() : 0;
    if (n > 255) return 0;
    const size_t need = 4 + (pull ? (1 + 4 * n) : 0) + (cfg ? 4 : 0);
    if (out.size() < need) return 0;
    wire::Writer w(out);
    w.u8(wire::cmd_byte(wire::Cmd::Q, static_cast<uint8_t>(in.leaf_id & 0x0F)));
    w.u8(in.src);
    w.u8(in.dest);
    w.u8(static_cast<uint8_t>(((static_cast<uint8_t>(in.opcode) & 0x03) << 6) |
                              (in.mobile ? (1u << 5) : 0u)));
    if (pull) {
        w.u8(static_cast<uint8_t>(n));
        for (uint32_t id : in.channel_ids) w.u32_be(id);
    } else if (cfg) {
        w.u16_le(in.pull_lineage);
        w.u16_le(in.pull_epoch);
    }
    return w.ok() ? w.size() : 0;
}

std::optional<q_out> parse_q(std::span<const uint8_t> frame) {
    if (frame.size() < 4) return std::nullopt;
    wire::Reader r(frame);
    const uint8_t b0 = r.u8();
    if (wire::cmd_of(b0) != wire::Cmd::Q) return std::nullopt;
    q_out o{};
    o.leaf_id = wire::flags_of(b0);
    o.src     = r.u8();
    o.dest    = r.u8();
    const uint8_t b3 = r.u8();
    if (!r.ok()) return std::nullopt;
    o.opcode = static_cast<uint8_t>((b3 >> 6) & 0x03);
    o.mobile = ((b3 >> 5) & 0x01) != 0;
    o.channel_id_count = 0; o.pull_lineage = 0; o.pull_epoch = 0;
    if (o.opcode == static_cast<uint8_t>(q_opcode::channel_pull)) {
        if (frame.size() < 5) return std::nullopt;
        const uint8_t count = frame[4];
        if (frame.size() < static_cast<size_t>(5) + static_cast<size_t>(count) * 4)
            return std::nullopt;
        o.channel_id_count = count;
    } else if (o.opcode == static_cast<uint8_t>(q_opcode::config_pull)) {   // R6.2: lineage u16 + epoch u16
        if (frame.size() < 8) return std::nullopt;
        o.pull_lineage = static_cast<uint16_t>(frame[4] | (frame[5] << 8));
        o.pull_epoch   = static_cast<uint16_t>(frame[6] | (frame[7] << 8));
    }
    return o;
}

std::optional<uint32_t> parse_q_channel_id(std::span<const uint8_t> frame,
                                           const q_out& q, uint8_t index) {
    if (index >= q.channel_id_count) return std::nullopt;
    const size_t off = static_cast<size_t>(5) + static_cast<size_t>(index) * 4;
    if (frame.size() < off + 4) return std::nullopt;
    wire::Reader r(frame.subspan(off, 4));
    return r.u32_be();
}

// -----------------------------------------------------------------------------
// H — cmd 0x7, 8 B (§3.7a). key_hash32 LE; byte 7 = H flags (bit 0 = HARD: skip the cache, reach the owner).
// -----------------------------------------------------------------------------
size_t pack_h(const h_in& in, std::span<uint8_t> out) {
    const size_t need = in.want_pubkey ? 8 + 32 : 8;       // §2: a WANT_PUBKEY H appends the requester's ed_pub[32]
    if (out.size() < need) return 0;
    wire::Writer w(out);
    w.u8(wire::cmd_byte(wire::Cmd::H, static_cast<uint8_t>(in.leaf_id & 0x0F)));
    w.u8(in.origin);
    w.u32_le(in.key_hash32);
    w.u8(in.ttl);                          // u8: config caps ttl <= 16
    w.u8(static_cast<uint8_t>((in.hard ? H_FLAG_HARD : 0) | (in.want_pubkey ? H_FLAG_WANT_PUBKEY : 0)));   // byte 7: H flags
    if (in.want_pubkey) for (int i = 0; i < 32; ++i) w.u8(in.requester_ed_pub[i]);   // bytes 8..39: the requester's ed_pub
    return w.ok() ? w.size() : 0;
}

std::optional<h_out> parse_h(std::span<const uint8_t> frame) {
    if (frame.size() < 7) return std::nullopt;
    wire::Reader r(frame);
    const uint8_t b0 = r.u8();
    if (wire::cmd_of(b0) != wire::Cmd::H) return std::nullopt;
    h_out o{};
    o.leaf_id    = wire::flags_of(b0);
    o.origin     = r.u8();
    o.key_hash32 = r.u32_le();
    o.ttl        = r.u8();
    if (!r.ok()) return std::nullopt;
    o.hard        = (frame.size() >= 8) && (frame[7] & H_FLAG_HARD);          // lenient: a 7-B frame parses as soft
    o.want_pubkey = (frame.size() >= 8) && (frame[7] & H_FLAG_WANT_PUBKEY);   // E2E §6: the sender wants the owner's ed_pub
    if (o.want_pubkey) {                                                      // §2: a WANT_PUBKEY H MUST carry the 32-B requester pubkey
        if (frame.size() < 8 + 32) return std::nullopt;                      // flag set but pubkey missing -> reject (fail loud)
        for (int i = 0; i < 32; ++i) o.requester_ed_pub[i] = frame[8 + i];
    }
    return o;
}

// -----------------------------------------------------------------------------
// F — cmd 0x8, 7 B. is_reply at byte2 bit 7; byte4 raw/by-flag (NOT interpreted);
// byte6 = relay (immediate forwarder) for metal-correct reverse/forward path learning.
// -----------------------------------------------------------------------------
size_t pack_f(const f_in& in, std::span<uint8_t> out) {
    if (out.size() < 9) return 0;          // R6.1: 7 + config_hash u16
    wire::Writer w(out);
    w.u8(wire::cmd_byte(wire::Cmd::F, static_cast<uint8_t>(in.leaf_id & 0x0F)));
    w.u8(in.origin);
    w.u8(in.is_reply ? 0x80 : 0x00);       // is_reply = bit 7; rsv = 0
    w.u8(in.dst_id);
    w.u8(in.ttl_or_next_hop);              // raw dual byte (ttl | next_hop)
    w.u8(in.hops);
    w.u8(in.relay);                        // byte 6: immediate forwarder's node_id
    w.u16_le(in.config_hash);              // bytes 7..8: R6.1 §6.4 leaf fingerprint (the F-flood membership gate)
    return w.ok() ? w.size() : 0;
}

std::optional<f_out> parse_f(std::span<const uint8_t> frame) {
    if (frame.size() < 9) return std::nullopt;   // R6.1 FLAG-DAY: config_hash is mandatory on F
    wire::Reader r(frame);
    const uint8_t b0 = r.u8();
    if (wire::cmd_of(b0) != wire::Cmd::F) return std::nullopt;
    f_out o{};
    o.leaf_id         = wire::flags_of(b0);
    o.origin          = r.u8();
    const uint8_t b2  = r.u8();
    o.dst_id          = r.u8();
    o.ttl_or_next_hop = r.u8();            // raw; handler interprets by is_reply
    o.hops            = r.u8();
    o.relay           = r.u8();            // byte 6: immediate forwarder
    o.config_hash     = r.u16_le();        // bytes 7..8: R6.1 §6.4 leaf fingerprint
    if (!r.ok()) return std::nullopt;
    o.is_reply = (b2 & 0x80) != 0;
    return o;
}

// -----------------------------------------------------------------------------
// J — join family, cmd 0x9. byte1 reading A: gw bit7, is_mobile bit6, opcode 5..4.
// All multi-byte fields LE. Exact length per opcode.
// -----------------------------------------------------------------------------
static inline uint8_t j_b0(uint8_t leaf) {
    return wire::cmd_byte(wire::Cmd::J, static_cast<uint8_t>(leaf & 0x0F));
}
static inline uint8_t j_b1(bool gw, bool mob, j_opcode op) {
    return static_cast<uint8_t>((gw ? 0x80 : 0) | (mob ? 0x40 : 0) |
                                ((static_cast<uint8_t>(op) & 0x03) << 4) |
                                (protocol::wire_version & 0x0F));   // R6.2 §5.2: stamp the wire_version in the rsv nibble (+0 B)
}

size_t pack_j_discover(const j_discover_in& in, std::span<uint8_t> out) {
    if (out.size() < 6) return 0;
    wire::Writer w(out);
    w.u8(j_b0(in.leaf_id));
    w.u8(j_b1(in.gateway_capable, in.is_mobile, j_opcode::discover));
    w.u32_le(in.key_hash32);
    return w.ok() ? w.size() : 0;
}

size_t pack_j_offer(const j_offer_in& in, std::span<uint8_t> out) {
    if (out.size() < (in.is_mobile ? 9u : 8u)) return 0;          // §mobile 2a: a mobile OFFER is 9 B (+proposed_mobile_id)
    wire::Writer w(out);
    w.u8(j_b0(in.leaf_id));
    w.u8(j_b1(in.gateway_capable, in.is_mobile, j_opcode::offer));
    w.u8(in.responder_node_id);
    w.u32_le(in.responder_key_hash32);
    w.u8(in.data_sf_bitmap);
    if (in.is_mobile) w.u8(in.proposed_mobile_id);               // §mobile 2a: the host-assigned LOCAL id (9-B mobile OFFER)
    return w.ok() ? w.size() : 0;
}

size_t pack_j_claim(const j_claim_in& in, std::span<uint8_t> out) {
    if (out.size() < 11) return 0;
    wire::Writer w(out);
    w.u8(j_b0(in.leaf_id));
    w.u8(j_b1(in.gateway_capable, in.is_mobile, j_opcode::claim));
    w.u32_le(in.key_hash32);
    w.u8(in.proposed_node_id);
    w.u16_le(in.lease_age_seconds);     // u16: producer saturates at 65535
    w.u8(in.claim_epoch);               // wrapping u8 counter (not saturated)
    w.u8(in.is_mobile ? in.chosen_host_id : in.nonce);   // §mobile: a mobile CLAIM carries the CHOSEN host id in byte-10 (not a nonce) so only that host records it; static packs the nonce (byte-identical)
    return w.ok() ? w.size() : 0;
}

size_t pack_j_deny(const j_deny_in& in, std::span<uint8_t> out) {
    if (out.size() < 15) return 0;
    wire::Writer w(out);
    w.u8(j_b0(in.leaf_id));
    w.u8(j_b1(in.gateway_capable, in.is_mobile, j_opcode::deny));
    w.u8(in.denied_node_id);
    w.u32_le(in.owner_key_hash32);
    w.u32_le(in.claimant_key_hash32);
    w.u16_le(in.owner_lease_age_seconds);
    w.u8(in.owner_claim_epoch);
    w.u8(in.reason);
    return w.ok() ? w.size() : 0;
}

std::optional<j_out> parse_j(std::span<const uint8_t> frame) {
    if (frame.size() < 2) return std::nullopt;
    wire::Reader r(frame);
    const uint8_t b0 = r.u8();
    if (wire::cmd_of(b0) != wire::Cmd::J) return std::nullopt;
    const uint8_t b1 = r.u8();
    j_out o{};
    o.leaf_id         = wire::flags_of(b0);
    o.gateway_capable = (b1 & 0x80) != 0;
    o.is_mobile       = (b1 & 0x40) != 0;
    o.opcode          = static_cast<uint8_t>((b1 >> 4) & 0x03);
    o.wire_version    = static_cast<uint8_t>(b1 & 0x0F);   // R6.2 §5.2 wire-compat (rsv nibble)
    switch (static_cast<j_opcode>(o.opcode)) {
        case j_opcode::discover:
            if (frame.size() != 6) return std::nullopt;
            o.key_hash32 = r.u32_le();
            break;
        case j_opcode::offer:
            if (frame.size() != (o.is_mobile ? 9u : 8u)) return std::nullopt;   // §mobile 2a: mobile OFFER is 9 B (exact-length per opcode)
            o.responder_node_id    = r.u8();
            o.responder_key_hash32 = r.u32_le();
            o.data_sf_bitmap       = r.u8();
            if (o.is_mobile) o.proposed_mobile_id = r.u8();       // §mobile 2a: host-assigned LOCAL id
            break;
        case j_opcode::claim:
            if (frame.size() != 11) return std::nullopt;
            o.key_hash32        = r.u32_le();
            o.proposed_node_id  = r.u8();
            o.lease_age_seconds = r.u16_le();
            o.claim_epoch       = r.u8();
            { const uint8_t b = r.u8(); o.nonce = b; o.chosen_host_id = b; }   // §mobile: same byte-10 -> static reads nonce, a mobile CLAIM reads chosen_host_id (the handler picks by is_mobile)
            break;
        case j_opcode::deny:
            if (frame.size() != 15) return std::nullopt;
            o.denied_node_id          = r.u8();
            o.owner_key_hash32        = r.u32_le();
            o.claimant_key_hash32     = r.u32_le();
            o.owner_lease_age_seconds = r.u16_le();
            o.owner_claim_epoch       = r.u8();
            o.reason                  = r.u8();
            break;
    }
    if (!r.ok()) return std::nullopt;
    return o;
}

// -----------------------------------------------------------------------------
// DATA — cmd=0x3, 12+n B (ROADMAP §10.3). See frame_codec.h for layout.
// -----------------------------------------------------------------------------
size_t pack_data(const data_in& in, std::span<uint8_t> out) {
    if (in.addr_len > 1) return 0;                                // §mobile Slice 1: 0=normal, 1=mobile-next (`next` is a local id); 2..7 hierarchy-deferred
    // CRYPTED ⇒ DST_HASH: the per-DM nonce derives from the CLEARTEXT dst_key_hash32, so it MUST be present
    // (spec §3 DP2). Refuse — never emit a sealed frame the recipient can't reconstruct the nonce for.
    if ((in.flags & DATA_FLAG_CRYPTED) && !(in.flags & DATA_FLAG_DST_HASH)) return 0;
    const size_t mac_len = data_mac_len(in.flags);                // 8-B nonce-seed under CRYPTED, else the 4-B MAC
    const bool mac_zero = in.mac.empty();
    if (!mac_zero && in.mac.size() != mac_len) return 0;

    const uint8_t hr = in.hops_remaining > 31 ? 31 : in.hops_remaining;   // saturate (matches Lua math.min)
    const uint8_t ch = in.committed_hops > 7  ? 7  : in.committed_hops;

    // APP is DERIVED from type: a non-zero type sets the byte-1 APP bit AND emits the TYPE byte at offset 8.
    // The caller never sets APP by hand, so the flag + type can't disagree.
    const uint8_t flags = static_cast<uint8_t>(in.type != 0 ? (in.flags | DATA_FLAG_APP)
                                                            : (in.flags & ~DATA_FLAG_APP));
    wire::Writer w(out);
    w.u8(wire::cmd_byte(wire::Cmd::D, static_cast<uint8_t>((in.addr_len & 0x07) << 1)));  // addr_len b3..1, rsv b0
    w.u8(flags);                                                 // full byte-1 flags
    w.u8(in.next);
    w.u8(in.dst);
    w.u8(static_cast<uint8_t>(((hr & 0x1F) << 3) | (ch & 0x07)));
    w.u8(in.prev_fwd_rt_hops);
    w.u16_le(in.ctr);
    if (in.type != 0) w.u8(in.type);                             // byte 8: TYPE (iff APP)
    for (uint8_t b : in.inner) w.u8(b);                           // opaque ciphertext slot
    if (mac_zero) { for (size_t i = 0; i < mac_len; ++i) w.u8(0); }
    else          { for (uint8_t b : in.mac) w.u8(b); }           // opaque trailer: 4-B MAC, or 8-B nonce-seed under CRYPTED
    return w.ok() ? w.size() : 0;
}

std::optional<data_out> parse_data(std::span<const uint8_t> frame) {
    if (frame.size() < DATA_HDR_LEN) return std::nullopt;          // need the 8-B header to read the byte-1 flags first
    if (wire::cmd_of(frame[0]) != wire::Cmd::D) return std::nullopt;

    data_out o{};
    o.addr_len = static_cast<uint8_t>((frame[0] >> 1) & 0x07);    // byte0 bits 3..1
    if (o.addr_len > 1) return std::nullopt;                      // §mobile Slice 1: 0=normal, 1=mobile-next; 2..7 hierarchy-deferred
    o.flags          = frame[1];                                 // full byte-1 flags
    o.app            = (o.flags & DATA_FLAG_APP)         != 0;
    o.cross_layer    = (o.flags & DATA_FLAG_CROSS_LAYER) != 0;
    o.crypted        = (o.flags & DATA_FLAG_CRYPTED)     != 0;
    o.e2e_ack_req    = (o.flags & DATA_FLAG_E2E_ACK_REQ) != 0;
    o.source_hash    = (o.flags & DATA_FLAG_SOURCE_HASH) != 0;
    o.dst_hash       = (o.flags & DATA_FLAG_DST_HASH)    != 0;
    o.priority       = (o.flags & DATA_FLAG_PRIORITY)    != 0;
    const size_t mac_len = data_mac_len(o.flags);                 // 8 under CRYPTED (nonce-seed), else the 4-B MAC
    if (frame.size() < DATA_HDR_LEN + mac_len) return std::nullopt;   // header + the (conditional) trailer
    o.next             = frame[2];
    o.dst              = frame[3];
    o.hops_remaining   = static_cast<uint8_t>((frame[4] >> 3) & 0x1F);
    o.committed_hops   = static_cast<uint8_t>(frame[4] & 0x07);
    o.prev_fwd_rt_hops = frame[5];
    { wire::Reader r(frame.subspan(6, 2)); o.ctr = r.u16_le(); }
    o.ctr_lo4     = static_cast<uint8_t>(o.ctr & 0x0F);           // derived hop-match convenience
    if (o.app) {
        // APP: a TYPE byte sits at offset 8 before the inner. The < 12 guard above only ensures the 8-B
        // header + 4-B MAC; an APP frame needs ONE more byte for the TYPE -> reject a too-short APP frame.
        if (frame.size() < DATA_HDR_LEN + 1 + mac_len) return std::nullopt;
        o.type      = frame[DATA_HDR_LEN];                        // byte 8
        o.inner_off = DATA_HDR_LEN + 1;                          // 9
    } else {
        o.type      = 0;
        o.inner_off = DATA_HDR_LEN;                              // 8
    }
    o.e2e_is_ack  = (o.type == DATA_TYPE_E2E_ACK);               // derived convenience
    o.inner_len   = frame.size() - o.inner_off - mac_len;        // size guard above ensures >= 0
    o.mac_off     = frame.size() - mac_len;
    o.frame_len   = frame.size();
    return o;
}

std::span<const uint8_t> data_inner(std::span<const uint8_t> frame, const data_out& d) {
    if (d.inner_off + d.inner_len > frame.size()) return {};
    return frame.subspan(d.inner_off, d.inner_len);
}
std::span<const uint8_t> data_mac(std::span<const uint8_t> frame, const data_out& d) {
    const size_t n = data_mac_len(d.flags);                       // 8 under CRYPTED (the nonce-seed), else 4
    if (d.mac_off + n > frame.size()) return {};
    return frame.subspan(d.mac_off, n);
}
// The 8-B cleartext nonce-seed (rand8) a CRYPTED DATA carries in its trailer; empty for a non-CRYPTED frame.
std::span<const uint8_t> data_nonce_seed(std::span<const uint8_t> frame, const data_out& d) {
    if (!d.crypted) return {};
    if (d.mac_off + 8 > frame.size()) return {};
    return frame.subspan(d.mac_off, 8);
}

// E2E observability: carve a CRYPTED DATA inner into [aad 4][ciphertext][tag 16] + the 8-B nonce-seed trailer.
crypted_region data_crypted_region(const data_out& d) {
    crypted_region r{};
    constexpr size_t kAadLen = 4;    // [dst_hash 4] — CRYPTED mandates DST_HASH (§1c: origin now SEALED in the ct)
    constexpr size_t kTagLen = 16;   // Poly1305 tag (== dm_crypto DM_TAG_LEN)
    if (!d.crypted) return r;                                   // not encrypted -> nothing to show
    if (d.inner_len < kAadLen + kTagLen) return r;             // malformed: can't hold aad + tag
    r.aad_off  = d.inner_off;                       r.aad_len  = kAadLen;
    r.ct_off   = d.inner_off + kAadLen;             r.ct_len   = d.inner_len - kAadLen - kTagLen;
    r.tag_off  = d.inner_off + d.inner_len - kTagLen; r.tag_len = kTagLen;
    r.seed_off = d.mac_off;                         r.seed_len = data_mac_len(d.flags);   // 8 under CRYPTED
    r.valid = true;
    return r;
}

std::optional<data_unicast_inner> parse_unicast_inner(std::span<const uint8_t> inner, uint8_t flags) {
    // Plaintext unicast: NO payload-flags byte. The optional dst_key_hash32 (4 B LE, the recipient's
    // key_hash32 — L2c verify-on-delivery) is present iff the byte-1 header had DST_HASH set, then origin,
    // then body. The presence comes from `flags`, not a leading inner byte.
    data_unicast_inner u{};
    size_t off = 0;
    if (flags & DATA_FLAG_DST_HASH) {
        if (inner.size() < off + 4) return std::nullopt;          // dst_key_hash32 (4 B LE)
        wire::Reader r(inner.subspan(off, 4)); u.dst_key_hash32 = r.u32_le();
        u.has_dst_hash = true; off += 4;
    }
    // Slice 4b: the CROSS_LAYER layer-path, BETWEEN dst_hash and origin (spec §5). n_layers:1 | cur:1 | n_layers×1B
    // FULL 8-bit ids. FAIL LOUD (nullopt) on short / n_layers 0 / n_layers > gw_env_max_hops / cur >= n_layers — NO
    // clamp, NO default-to-0 (extends the existing nullopt-on-short precedent; the consumer must refuse, not raw-deliver).
    if (flags & DATA_FLAG_CROSS_LAYER) {
        if (inner.size() < off + 2) return std::nullopt;          // n_layers + cur
        const uint8_t n = inner[off]; const uint8_t cur = inner[off + 1];
        if (n == 0 || n > protocol::gw_env_max_hops) return std::nullopt;
        if (cur >= n) return std::nullopt;                        // cur indexes the NEXT layer to enter (< n)
        if (inner.size() < off + 2 + n) return std::nullopt;      // the n layer_ids
        u.has_cross_layer = true; u.n_layers = n; u.cur = cur;
        for (uint8_t i = 0; i < n; ++i) u.layer_ids[i] = inner[off + 2 + i];
        off += static_cast<size_t>(2) + n;
    }
    if (flags & DATA_FLAG_CRYPTED) {
        // §1c SEALED: origin + source_hash + location + body ALL live INSIDE the ciphertext (+ a 16-B tag at the end).
        // The cleartext region ends at dst_hash (+ any cross-layer path); hand the rest (ciphertext||tag) to the open
        // step. u.origin stays 0/unset — a relay must NOT learn who originated a CRYPTED DM (privacy property).
        u.body = inner.subspan(off);
        return u;
    }
    if (inner.size() < off + 1) return std::nullopt;              // origin (plaintext only)
    u.origin = inner[off]; off += 1;
    if (flags & DATA_FLAG_SOURCE_HASH) {                          // origin's key_hash32 (stable sender id), after origin
        if (inner.size() < off + 4) return std::nullopt;          // source_hash (4 B LE)
        wire::Reader r(inner.subspan(off, 4)); u.source_hash = r.u32_le();
        u.has_source_hash = true; off += 4;
    }
    if (flags & DATA_FLAG_LOCATION) {                            // 6-B sender location, after source_hash, before body
        if (inner.size() < off + 6) return std::nullopt;
        int32_t la = 0, lo = 0; unpack_loc6(inner.subspan(off, 6), la, lo);
        u.has_location = true; u.lat_e7 = la; u.lon_e7 = lo; off += 6;
    }
    u.body   = inner.subspan(off);
    return u;
}

// 6-byte location codec — 21-bit lat + 22-bit lon, ~11 m. Quantize the stored int32 deg×1e7 by 1024
// (step ≈ 0.0001024° ≈ 11.4 m); +512 on decode centres the cell (per-axis error ≤ 512 e7 ≈ 5.7 m).
// Pack the 48-bit value (u_lat<<27)|(u_lon<<5) MSB-first; the low 5 bits are reserved = 0.
size_t pack_loc6(int32_t lat_e7, int32_t lon_e7, std::span<uint8_t> out6) {
    if (out6.size() < 6) return 0;
    int64_t u_lat = (static_cast<int64_t>(lat_e7) + 900000000) >> 10;       // int64: lat_e7+9e8 can exceed int32
    int64_t u_lon = (static_cast<int64_t>(lon_e7) + 1800000000) >> 10;
    if (u_lat < 0) u_lat = 0; else if (u_lat > (1 << 21) - 1) u_lat = (1 << 21) - 1;   // clamp to 21 bits
    if (u_lon < 0) u_lon = 0; else if (u_lon > (1 << 22) - 1) u_lon = (1 << 22) - 1;   // clamp to 22 bits
    const uint64_t v = (static_cast<uint64_t>(u_lat) << 27) | (static_cast<uint64_t>(u_lon) << 5);
    out6[0] = static_cast<uint8_t>(v >> 40); out6[1] = static_cast<uint8_t>(v >> 32);
    out6[2] = static_cast<uint8_t>(v >> 24); out6[3] = static_cast<uint8_t>(v >> 16);
    out6[4] = static_cast<uint8_t>(v >> 8);  out6[5] = static_cast<uint8_t>(v);
    return 6;
}
bool unpack_loc6(std::span<const uint8_t> in6, int32_t& lat_e7, int32_t& lon_e7) {
    if (in6.size() < 6) return false;
    const uint64_t v = (static_cast<uint64_t>(in6[0]) << 40) | (static_cast<uint64_t>(in6[1]) << 32) |
                       (static_cast<uint64_t>(in6[2]) << 24) | (static_cast<uint64_t>(in6[3]) << 16) |
                       (static_cast<uint64_t>(in6[4]) << 8)  |  static_cast<uint64_t>(in6[5]);
    const uint32_t u_lat = static_cast<uint32_t>((v >> 27) & ((1u << 21) - 1));
    const uint32_t u_lon = static_cast<uint32_t>((v >> 5)  & ((1u << 22) - 1));
    // int64 intermediates are MANDATORY: u_lon<<10 reaches 3.6e9 > INT32_MAX before the offset brings it back.
    lat_e7 = static_cast<int32_t>((static_cast<int64_t>(u_lat) << 10) - 900000000  + 512);
    lon_e7 = static_cast<int32_t>((static_cast<int64_t>(u_lon) << 10) - 1800000000 + 512);
    return true;
}

size_t pack_unicast_inner(std::span<uint8_t> out, uint8_t flags, uint32_t dst_key_hash32,
                          const uint8_t* layer_ids, uint8_t n_layers, uint8_t cur,
                          uint8_t origin, uint32_t source_hash, const uint8_t* body, uint8_t body_len,
                          int32_t lat_e7, int32_t lon_e7) {
    // Size the inner FIRST so a too-small `out` never gets a partial write (fail loud: return 0, the caller refuses).
    size_t need = static_cast<size_t>(1) + body_len;             // origin + body
    if (flags & DATA_FLAG_DST_HASH)    need += 4;
    if (flags & DATA_FLAG_SOURCE_HASH) need += 4;
    if (flags & DATA_FLAG_LOCATION)    need += 6;                // 6-B sender location (after source_hash) — body cap shrinks by 6
    if (flags & DATA_FLAG_CROSS_LAYER) {
        if (n_layers == 0 || n_layers > protocol::gw_env_max_hops || cur >= n_layers) return 0;   // invalid path -> refuse
        need += static_cast<size_t>(2) + n_layers;
    }
    if (need > out.size()) return 0;                             // overflow -> refuse (NO truncation)
    size_t off = 0;
    if (flags & DATA_FLAG_DST_HASH) {                            // dst_key_hash32 (4 B LE) — matches enqueue_data's manual LE
        out[off++] = static_cast<uint8_t>(dst_key_hash32);       out[off++] = static_cast<uint8_t>(dst_key_hash32 >> 8);
        out[off++] = static_cast<uint8_t>(dst_key_hash32 >> 16); out[off++] = static_cast<uint8_t>(dst_key_hash32 >> 24);
    }
    if (flags & DATA_FLAG_CROSS_LAYER) {                         // layer-path: n_layers | cur | layer_ids (FULL 8-bit)
        out[off++] = n_layers; out[off++] = cur;
        for (uint8_t i = 0; i < n_layers; ++i) out[off++] = layer_ids[i];
    }
    out[off++] = origin;
    if (flags & DATA_FLAG_SOURCE_HASH) {                         // source_hash (4 B LE), AFTER origin
        out[off++] = static_cast<uint8_t>(source_hash);       out[off++] = static_cast<uint8_t>(source_hash >> 8);
        out[off++] = static_cast<uint8_t>(source_hash >> 16); out[off++] = static_cast<uint8_t>(source_hash >> 24);
    }
    if (flags & DATA_FLAG_LOCATION) {                            // 6-B location, AFTER source_hash, BEFORE body (origin-onward sealed region)
        pack_loc6(lat_e7, lon_e7, out.subspan(off, 6)); off += 6;
    }
    for (uint8_t i = 0; i < body_len; ++i) out[off++] = body ? body[i] : 0;
    return off;
}
// -----------------------------------------------------------------------------
// M — lean channel-message frame (cmd 0xA, 7+n B). 2026-06-09: channel messages
// moved off the DATA frame onto their own cmd. leaf_id rides byte-0 (the leak gate).
// -----------------------------------------------------------------------------
size_t pack_m(const m_in& in, std::span<uint8_t> out) {
    if (out.size() < M_FRAME_HDR_LEN + in.body.size()) return 0;
    wire::Writer w(out);
    w.u8(wire::cmd_byte(wire::Cmd::M, static_cast<uint8_t>(in.leaf_id & 0x0F)));
    w.u8(in.channel_id);
    w.u8(in.flavor);
    w.u32_be(in.channel_msg_id);                                  // BIG-endian (origin = byte 3)
    for (uint8_t b : in.body) w.u8(b);
    return w.ok() ? w.size() : 0;
}
std::optional<m_out> parse_m(std::span<const uint8_t> frame) {
    if (frame.size() < M_FRAME_HDR_LEN) return std::nullopt;      // cmd|leaf + channel_id + flavor + id(4)
    if (wire::cmd_of(frame[0]) != wire::Cmd::M) return std::nullopt;
    m_out o{};
    o.leaf_id    = wire::flags_of(frame[0]);                      // the leak gate (checked by the caller)
    o.channel_id = frame[1];
    o.flavor     = frame[2];
    { wire::Reader r(frame.subspan(3, 4)); o.channel_msg_id = r.u32_be(); }   // BIG-endian
    o.body       = frame.subspan(M_FRAME_HDR_LEN);
    return o;
}

// 6-B hash-bind inner: NO payload-flags byte (H_ANSWER + AUTHORITATIVE ride the frame TYPE). The caller
// sets the frame's data_in.type from `in.authoritative` (H_ANSWER vs AUTHORITATIVE_H_ANSWER).
size_t pack_hash_bind_inner(const hash_bind_inner& in, std::span<uint8_t> out) {
    if (out.size() < 6) return 0;
    wire::Writer w(out);
    w.u8(in.target_layer);
    w.u8(in.node_id);
    w.u32_le(in.key_hash32);                                      // LITTLE-endian (matches pack_h / beacon)
    return w.ok() ? w.size() : 0;
}
// The caller already knows it's a hash-bind from the frame TYPE, so there's no flags byte to check here.
// `authoritative` is left default (the caller sets the binding confidence from the TYPE byte, not the inner).
std::optional<hash_bind_inner> parse_hash_bind_inner(std::span<const uint8_t> inner) {
    if (inner.size() < 6) return std::nullopt;
    hash_bind_inner o{};
    o.target_layer = inner[0];
    o.node_id      = inner[1];
    { wire::Reader r(inner.subspan(2, 4)); o.key_hash32 = r.u32_le(); }
    return o;
}

// Hash-bind PUBKEY answer inner (E2E §6, TYPE 5): [target_layer][node_id][ed_pub 32] = 34 B (key_hash32 dropped).
size_t pack_hash_bind_pubkey_inner(const hash_bind_pubkey_inner& in, std::span<uint8_t> out) {
    if (out.size() < 34) return 0;
    out[0] = in.target_layer; out[1] = in.node_id;
    for (int i = 0; i < 32; ++i) out[2 + i] = in.ed_pub[i];
    return 34;
}
std::optional<hash_bind_pubkey_inner> parse_hash_bind_pubkey_inner(std::span<const uint8_t> inner) {
    if (inner.size() < 34) return std::nullopt;
    hash_bind_pubkey_inner o{};
    o.target_layer = inner[0]; o.node_id = inner[1];
    for (int i = 0; i < 32; ++i) o.ed_pub[i] = inner[2 + i];
    return o;
}

}  // namespace meshroute
