// MeshRoute — frame_codec.cpp
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

namespace meshroute {

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
    w.u8(static_cast<uint8_t>(((n_entries >> 3) & 0x07) << 5));   // n_entries_hi | rsv=0
    w.u32_le(in.key_hash32);

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
        w.u8(static_cast<uint8_t>(((e.score_bucket & 0x0F) << 4) | (e.is_gateway ? 0x01 : 0x00)));
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

std::optional<beacon_out> parse_beacon(std::span<const uint8_t> frame) {
    if (frame.size() < 8) return std::nullopt;
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
    { wire::Reader r(frame.subspan(4, 4)); o.key_hash32 = r.u32_le(); }

    size_t pos = 8;
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

// -----------------------------------------------------------------------------
// CTS — cmd=0x2, 3 B (ROADMAP §10.3)
// -----------------------------------------------------------------------------
size_t pack_cts(const cts_in& in, std::span<uint8_t> out) {
    if (in.chosen_data_sf < 5 || in.chosen_data_sf > 12) return 0;
    if (out.size() < 3) return 0;
    wire::Writer w(out);
    w.u8(wire::cmd_byte(wire::Cmd::C, static_cast<uint8_t>(in.ctr_lo & 0x0F)));
    const uint8_t sf3 = static_cast<uint8_t>((in.chosen_data_sf - 5) & 0x07);
    w.u8(static_cast<uint8_t>((sf3 << 5) | (in.already_received ? 0x10 : 0x00)));
    w.u8(in.to);
    return w.ok() ? w.size() : 0;
}

std::optional<cts_out> parse_cts(std::span<const uint8_t> frame) {
    if (frame.size() != 3) return std::nullopt;
    wire::Reader r(frame);
    const uint8_t b0 = r.u8();
    if (wire::cmd_of(b0) != wire::Cmd::C) return std::nullopt;
    const uint8_t b1 = r.u8();
    const uint8_t to = r.u8();
    if (!r.ok()) return std::nullopt;
    cts_out o{};
    o.ctr_lo           = wire::flags_of(b0);
    o.chosen_data_sf   = static_cast<uint8_t>(((b1 >> 5) & 0x07) + 5);
    o.already_received = (b1 & 0x10) != 0;
    o.to               = to;
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
    w.u8(static_cast<uint8_t>((bh << 6) | ((in.snr_bucket & 0x03) << 4)));
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
    return o;
}

// -----------------------------------------------------------------------------
// RTS — cmd 0x1, 7 B (+2 if M_BROADCAST). byte-5 reading A: flags at bits 5..2.
// -----------------------------------------------------------------------------
size_t pack_rts(const rts_in& in, std::span<uint8_t> out) {
    const bool m_bcast = (in.rts_flags & RTS_FLAG_M_BROADCAST) != 0;
    const size_t need = m_bcast ? 9 : 7;
    if (out.size() < need) return 0;
    wire::Writer w(out);
    w.u8(wire::cmd_byte(wire::Cmd::R, static_cast<uint8_t>(in.leaf_id & 0x0F)));
    w.u8(in.src);
    w.u8(in.next);
    w.u8(static_cast<uint8_t>((in.ctr_lo & 0x0F) << 4));            // ctr_lo hi; addr_len=0, rsv=0
    w.u8(in.dst);
    w.u8(static_cast<uint8_t>(((in.sf_index & 0x03) << 6) |        // reading A: sf_index 7..6,
                              ((in.rts_flags & 0x0F) << 2)));      //            rts_flags 5..2, rsv 1..0
    w.u8(in.payload_len);                                          // mod-256 enforced by uint8_t
    if (m_bcast) w.u16_be(in.m_payload_id_lo16);
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
    if (o.addr_len != 0) return std::nullopt;                      // hierarchy deferred
    o.sf_index  = static_cast<uint8_t>((b5 >> 6) & 0x03);
    o.rts_flags = static_cast<uint8_t>((b5 >> 2) & 0x0F);          // reading A
    o.m_broadcast = (o.rts_flags & RTS_FLAG_M_BROADCAST) != 0;
    o.m_payload_id_lo16 = 0;
    if (o.m_broadcast && frame.size() >= 9) o.m_payload_id_lo16 = r.u16_be();
    return o;
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
    const bool pull = (in.opcode == q_opcode::channel_pull);
    const size_t n = pull ? in.channel_ids.size() : 0;
    if (n > 255) return 0;
    const size_t need = 4 + (pull ? (1 + 4 * n) : 0);
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
    o.channel_id_count = 0;
    if (o.opcode == static_cast<uint8_t>(q_opcode::channel_pull)) {
        if (frame.size() < 5) return std::nullopt;
        const uint8_t count = frame[4];
        if (frame.size() < static_cast<size_t>(5) + static_cast<size_t>(count) * 4)
            return std::nullopt;
        o.channel_id_count = count;
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
// H — cmd 0x7, 7 B (§10.6 flag byte dropped, lossless). key_hash32 LE.
// -----------------------------------------------------------------------------
size_t pack_h(const h_in& in, std::span<uint8_t> out) {
    if (out.size() < 7) return 0;
    wire::Writer w(out);
    w.u8(wire::cmd_byte(wire::Cmd::H, static_cast<uint8_t>(in.leaf_id & 0x0F)));
    w.u8(in.origin);
    w.u32_le(in.key_hash32);
    w.u8(in.ttl);                          // u8: config caps ttl <= 16
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
    return o;
}

// -----------------------------------------------------------------------------
// F — cmd 0x8, 6 B. is_reply at byte2 bit 7; byte4 raw/by-flag (NOT interpreted).
// -----------------------------------------------------------------------------
size_t pack_f(const f_in& in, std::span<uint8_t> out) {
    if (out.size() < 6) return 0;
    wire::Writer w(out);
    w.u8(wire::cmd_byte(wire::Cmd::F, static_cast<uint8_t>(in.leaf_id & 0x0F)));
    w.u8(in.origin);
    w.u8(in.is_reply ? 0x80 : 0x00);       // is_reply = bit 7; rsv = 0
    w.u8(in.dst_id);
    w.u8(in.ttl_or_next_hop);              // raw dual byte (ttl | next_hop)
    w.u8(in.hops);
    return w.ok() ? w.size() : 0;
}

std::optional<f_out> parse_f(std::span<const uint8_t> frame) {
    if (frame.size() < 6) return std::nullopt;
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
                                ((static_cast<uint8_t>(op) & 0x03) << 4));
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
    if (out.size() < 8) return 0;
    wire::Writer w(out);
    w.u8(j_b0(in.leaf_id));
    w.u8(j_b1(in.gateway_capable, in.is_mobile, j_opcode::offer));
    w.u8(in.responder_node_id);
    w.u32_le(in.responder_key_hash32);
    w.u8(in.data_sf_bitmap);
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
    w.u8(in.nonce);
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
    switch (static_cast<j_opcode>(o.opcode)) {
        case j_opcode::discover:
            if (frame.size() != 6) return std::nullopt;
            o.key_hash32 = r.u32_le();
            break;
        case j_opcode::offer:
            if (frame.size() != 8) return std::nullopt;
            o.responder_node_id    = r.u8();
            o.responder_key_hash32 = r.u32_le();
            o.data_sf_bitmap       = r.u8();
            break;
        case j_opcode::claim:
            if (frame.size() != 11) return std::nullopt;
            o.key_hash32        = r.u32_le();
            o.proposed_node_id  = r.u8();
            o.lease_age_seconds = r.u16_le();
            o.claim_epoch       = r.u8();
            o.nonce             = r.u8();
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
// DATA — cmd=0x3, 18+n B (ROADMAP §10.3). See frame_codec.h for layout.
// -----------------------------------------------------------------------------
size_t pack_data(const data_in& in, std::span<uint8_t> out) {
    if (in.addr_len != 0) return 0;                               // hierarchy deferred this phase
    const bool vis_zero = in.visited.empty();
    const bool mac_zero = in.mac.empty();
    if (!vis_zero && in.visited.size() != DATA_VISITED_LEN) return 0;
    if (!mac_zero && in.mac.size()     != DATA_MAC_LEN)     return 0;

    const uint8_t hr = in.hops_remaining > 31 ? 31 : in.hops_remaining;   // saturate (matches Lua math.min)
    const uint8_t ch = in.committed_hops > 7  ? 7  : in.committed_hops;

    wire::Writer w(out);
    w.u8(wire::cmd_byte(wire::Cmd::D, static_cast<uint8_t>((in.addr_len & 0x07) << 1)));  // addr_len b3..1, rsv b0
    w.u8(static_cast<uint8_t>((in.flags & 0x0F) << 4));           // flags high nibble | rsv low
    w.u8(in.next);
    w.u8(in.dst);
    w.u8(static_cast<uint8_t>(((hr & 0x1F) << 3) | (ch & 0x07)));
    w.u8(in.prev_fwd_rt_hops);
    w.u16_le(in.ctr);
    if (vis_zero) { for (int i = 0; i < (int)DATA_VISITED_LEN; ++i) w.u8(0); }
    else          { for (uint8_t b : in.visited) w.u8(b); }
    for (uint8_t b : in.inner) w.u8(b);                           // opaque ciphertext slot
    if (mac_zero) { for (int i = 0; i < (int)DATA_MAC_LEN; ++i) w.u8(0); }
    else          { for (uint8_t b : in.mac) w.u8(b); }           // opaque 4-B trailer
    return w.ok() ? w.size() : 0;
}

std::optional<data_out> parse_data(std::span<const uint8_t> frame) {
    if (frame.size() < DATA_HDR_LEN + DATA_MAC_LEN) return std::nullopt;   // < 18
    if (wire::cmd_of(frame[0]) != wire::Cmd::D) return std::nullopt;

    data_out o{};
    o.addr_len = static_cast<uint8_t>((frame[0] >> 1) & 0x07);    // byte0 bits 3..1
    if (o.addr_len != 0) return std::nullopt;                     // hierarchy deferred
    o.flags          = static_cast<uint8_t>((frame[1] >> 4) & 0x0F);
    o.e2e_ack_req    = (o.flags & DATA_FLAG_E2E_ACK_REQ)    != 0;
    o.e2e_is_ack     = (o.flags & DATA_FLAG_E2E_IS_ACK)     != 0;
    o.priority       = (o.flags & DATA_FLAG_PRIORITY)       != 0;
    o.payload_type_m = (o.flags & DATA_FLAG_PAYLOAD_TYPE_M) != 0;
    o.next             = frame[2];
    o.dst              = frame[3];
    o.hops_remaining   = static_cast<uint8_t>((frame[4] >> 3) & 0x1F);
    o.committed_hops   = static_cast<uint8_t>(frame[4] & 0x07);
    o.prev_fwd_rt_hops = frame[5];
    { wire::Reader r(frame.subspan(6, 2)); o.ctr = r.u16_le(); }
    o.ctr_lo4     = static_cast<uint8_t>(o.ctr & 0x0F);           // derived hop-match convenience
    o.visited_off = 8;
    o.inner_off   = DATA_HDR_LEN;                                 // 14
    o.inner_len   = frame.size() - DATA_HDR_LEN - DATA_MAC_LEN;   // size>=18 guaranteed above
    o.mac_off     = frame.size() - DATA_MAC_LEN;
    o.frame_len   = frame.size();
    return o;
}

std::span<const uint8_t> data_visited(std::span<const uint8_t> frame, const data_out& d) {
    if (d.visited_off + DATA_VISITED_LEN > frame.size()) return {};
    return frame.subspan(d.visited_off, DATA_VISITED_LEN);
}
std::span<const uint8_t> data_inner(std::span<const uint8_t> frame, const data_out& d) {
    if (d.inner_off + d.inner_len > frame.size()) return {};
    return frame.subspan(d.inner_off, d.inner_len);
}
std::span<const uint8_t> data_mac(std::span<const uint8_t> frame, const data_out& d) {
    if (d.mac_off + DATA_MAC_LEN > frame.size()) return {};
    return frame.subspan(d.mac_off, DATA_MAC_LEN);
}

std::optional<data_unicast_inner> parse_unicast_inner(std::span<const uint8_t> inner) {
    if (inner.size() < 2) return std::nullopt;                    // src_addr_len + origin
    if (inner[0] != 0) return std::nullopt;                       // src_addr_len must be 0 this phase
    data_unicast_inner u{};
    u.origin = inner[1];
    u.body   = inner.subspan(2);
    return u;
}
std::optional<data_m_inner> parse_m_inner(std::span<const uint8_t> inner) {
    if (inner.size() < 6) return std::nullopt;                    // channel_msg_id(4) + channel_id + flavor
    data_m_inner m{};
    { wire::Reader r(inner.subspan(0, 4)); m.channel_msg_id = r.u32_be(); }   // BIG-endian
    m.channel_id = inner[4];
    m.flavor     = inner[5];
    m.body       = inner.subspan(6);
    return m;
}

}  // namespace meshroute
