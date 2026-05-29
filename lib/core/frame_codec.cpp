// MeshRoute — frame_codec.cpp
//
// §10 cmd-nibble wire codecs. Implemented: CTS (cmd 0x2) + ACK (cmd 0x4)
// (codec track C1). BCN is still an iteration-1 stub (returns 0/nullopt),
// reworked to the §10 layout at C5 — see the STALE-struct note in frame_codec.h.
//
// Wire authority: byte positions = ROADMAP §10.3. The C++ wire DIVERGES from the
// Lua tag-byte wire by design (cmd-nibble, shorter frames) — NOT byte-for-byte.
// Field *meaning* = the Lua pack_*/parse_* code (dv_dual_sf.lua); genuine Lua
// bugs are fixed in both sides.

#include "frame_codec.h"

#include "wire.h"

namespace meshroute {

size_t pack_beacon(const beacon_in& /*in*/, std::span<uint8_t> /*out*/) {
    // TODO(C5): port the beacon codec in the §10 cmd-nibble layout (frame_codec.h):
    //   - byte 0 = cmd 0x0(4 hi) | leaf_id(4 lo)   — the short cmd code, NOT 'B'
    //   - byte 1 = src; byte 2 = flags + n_entries; bytes 4..7 = key_hash32 LE
    //   - [schedule block]; 4-byte route entries × n; [seen_bitmap 32 B]; [ext]
    //   - pursue the §10.6 shortening: drop n_entries (derive from frame_len).
    return 0;
}

std::optional<beacon_out> parse_beacon(std::span<const uint8_t> /*frame*/) {
    // TODO(C5): validate cmd nibble 0x0 (not tag 'B') + min 8-B header, unpack the
    // §10 fields, bounds-check the 4-byte entries + optional schedule/bitmap/ext.
    return std::nullopt;
}

std::optional<beacon_entry> parse_beacon_entry(std::span<const uint8_t> /*frame*/,
                                                const beacon_out& /*bcn*/,
                                                uint8_t /*index*/) {
    // TODO(C5): walk the 4-byte entries at the right §10 offset.
    return std::nullopt;
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

}  // namespace meshroute
