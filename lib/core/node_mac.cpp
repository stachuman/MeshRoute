// MeshRoute — lib/core/node_mac.cpp  (R3 MAC data plane: RTS-CTS-DATA-ACK-NACK)
//
// RTS/CTS/ACK ride routing_sf; only DATA rides the chosen data_sf (per-frame
// TxParams.sf). The SENDER never retunes its RX; only the RECEIVER does
// (set_rx_sf(data_sf) after CTS, back to routing_sf at DATA/expiry). Holds the
// send queue drain, the RTS/CTS/DATA/ACK/NACK frame handlers, the post-ACK
// deliver/forward, and the timeout-start helpers. Behaviour mirrors dv_dual_sf.lua.
// Part of the Node class (declared in node.h); split out of node.cpp for readability.
// See docs/specs/2026-05-30-r3-data-plane-design.md.
#include "node.h"

#include "frame_codec.h"
#include "airtime.h"

#include <span>

namespace meshroute {

// 2-bit ACK SNR bucket (dv_dual_sf.lua:842; centers -16/-8/+4) — NOT the 4-bit one.
static uint8_t bucket_of_snr_2b(int snr_q4) {
    if (snr_q4 < -192) return 0;        // < -12 dB
    if (snr_q4 <  -64) return 1;        // < -4 dB
    return 2;
}

uint16_t Node::next_ctr(uint8_t dst) {
    uint16_t& c = _peer_send_counter[dst];
    c = (c >= 65535) ? 1 : static_cast<uint16_t>(c + 1);   // wraps 65535->1 (NOT a rand site)
    return c;
}

uint8_t Node::select_data_sf(uint8_t rts_sf_index) const {
    // sf_index=3 (ANY) -> our preferred data SF. 0..2 (pinned) deferred — the R3
    // gate uses ANY=3 (decision Q4). PURE (no rand) — dv_dual_sf.lua:3027.
    (void)rts_sf_index;
    return _cfg.data_sf;
}

uint32_t Node::airtime_routing_ms(uint16_t len) const {
    return airtime_ms(_cfg.routing_sf, _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym, len);
}
// 3*airtime(routing, Lua RTS_LEN=8) — a TIMING constant from the Lua, NOT the 7-B C++ wire,
// so the retry rand RANGE matches the Lua and the lua-vs-meshroute streams stay aligned.
uint32_t Node::retry_jitter_ms() const { return 3 * airtime_routing_ms(8); }

uint16_t Node::do_send(uint8_t dst, const uint8_t* body, uint8_t body_len, uint8_t flags) {
    const uint16_t ctr = next_ctr(dst);
    TxItem item{};
    item.origin = _node_id; item.dst = dst; item.ctr = ctr; item.ctr_lo = static_cast<uint8_t>(ctr & 0x0F);
    item.flags = flags;     // E2E/PRIORITY ride the wire; their behaviour (ack-track/budget) is a later iteration
    item.inner[0] = 0x00; item.inner[1] = _node_id;      // src_addr_len=0 | origin | body
    if (body) for (uint8_t i = 0; i < body_len; ++i) item.inner[2 + i] = body[i];
    item.inner_len = static_cast<uint8_t>(2 + body_len);
    item.enqueue_time_ms = _hal.now();                   // first-enqueue time (cascade-requeue total-age cap)
    if (_tx_queue_n < kTxQueueCap) _tx_queue[_tx_queue_n++] = item;
    EventField f[] = {
        { .key = "origin", .type = EventField::T::i64, .i = item.origin },
        { .key = "dst",    .type = EventField::T::i64, .i = item.dst },
        { .key = "ctr",    .type = EventField::T::i64, .i = item.ctr },
        { .key = "depth",  .type = EventField::T::i64, .i = _tx_queue_n },
    };
    _hal.emit("tx_enqueue", f, 4);                       // dm_delivery record-creation key (fid==origin)
    become_free();
    return ctr;
}

void Node::become_free() {
    if (_pending_tx || _pending_rx) return;              // half-duplex serialize
    if (_tx_queue_n == 0) return;
    // Drain the FIRST item whose backoff has elapsed (next_attempt_ms <= now). The
    // Lua scans (not head-only) so a fresh send isn't blocked behind a backing-off
    // cascade-requeue, and the next_attempt_ms gate means a concurrent become_free
    // can't skip a requeue's backoff. Items with next_attempt_ms==0 are always ready,
    // so a queue without requeues behaves as plain FIFO.
    const uint64_t now = _hal.now();
    uint8_t  pick = _tx_queue_n;                          // sentinel = none ready
    uint64_t soonest = UINT64_MAX;
    for (uint8_t i = 0; i < _tx_queue_n; ++i) {
        if (_tx_queue[i].next_attempt_ms <= now) { pick = i; break; }
        if (_tx_queue[i].next_attempt_ms < soonest) soonest = _tx_queue[i].next_attempt_ms;
    }
    if (pick == _tx_queue_n) {                            // none ready -> wake at the soonest backoff
        (void)_hal.after(static_cast<uint32_t>(soonest - now), kQueueWakeupTimerId);
        return;
    }
    TxItem item = _tx_queue[pick];
    for (uint8_t i = pick + 1; i < _tx_queue_n; ++i) _tx_queue[i - 1] = _tx_queue[i];
    --_tx_queue_n;
    issue_send(item);
}

void Node::issue_send(const TxItem& item) {
    // Build the flight first so pick_next_cascade_hop sees previous_hop (forwarders
    // must not loop back upstream) + the empty alts_tried set.
    PendingTx pt{};
    pt.origin = item.origin; pt.dst = item.dst;
    pt.ctr_lo = item.ctr_lo; pt.ctr = item.ctr; pt.flags = item.flags;
    pt.inner_len = item.inner_len;
    for (uint8_t i = 0; i < item.inner_len; ++i) pt.inner[i] = item.inner[i];
    pt.chosen_data_sf = 0; pt.retries_left = effective_rts_max_retries(item.requeue_count);
    pt.awaiting_cts = true; pt.awaiting_ack = false;
    pt.alts_tried_n = 0;
    pt.previous_hop = item.previous_hop; pt.has_previous_hop = item.is_forward;
    pt.requeue_count = item.requeue_count; pt.enqueue_time_ms = item.enqueue_time_ms;

    const uint8_t first = pick_next_cascade_hop(pt);     // first SELECTABLE candidate (skips previous_hop)
    if (first == 0) {
        // No usable route yet. A FORWARDER drops (dv_dual_sf.lua:7041-7048 — it
        // can't hold someone else's transit); an ORIGINATOR defers the message
        // until a beacon installs a route or the defer-TTL expires (dv:7049-7052).
        if (item.is_forward) {
            EventField f[] = { { .key = "dst", .type = EventField::T::i64, .i = item.dst } };
            _hal.emit("send_no_route", f, 1);
        } else {
            defer_send(item);
        }
        return;
    }
    pt.next = first;
    _pending_tx = pt;
    tx_rts_retry();                                      // packs+emits the RTS + start_rts_timeout
}

void Node::tx_rts_retry() {
    if (!_pending_tx) return;
    PendingTx& pt = *_pending_tx;
    pt.awaiting_cts = true; pt.awaiting_ack = false; pt.chosen_data_sf = 0;
    rts_in rin{};
    rin.leaf_id = _cfg.leaf_id; rin.src = _node_id; rin.next = pt.next; rin.ctr_lo = pt.ctr_lo;
    rin.dst = pt.dst; rin.sf_index = 3 /*ANY*/; rin.rts_flags = 0;
    rin.payload_len = static_cast<uint8_t>(pt.inner_len + 4 /*MAC_LEN*/); rin.m_payload_id_lo16 = 0;
    uint8_t buf[9];
    const size_t l = pack_rts(rin, std::span<uint8_t>(buf, sizeof(buf)));
    if (l == 0) { _hal.log("RTS pack failed"); return; }
    TxParams p; p.sf = static_cast<int16_t>(_cfg.routing_sf); p.label = "RTS";
    _hal.tx(buf, l, p);                                  // RX stays on routing_sf
    EventField f[] = {
        { .key = "dst",  .type = EventField::T::i64, .i = pt.dst },
        { .key = "next", .type = EventField::T::i64, .i = pt.next },
        { .key = "ctr",  .type = EventField::T::i64, .i = pt.ctr },
    };
    _hal.emit("rts_tx", f, 3);
    start_rts_timeout();
}

void Node::handle_rts(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    (void)meta;
    auto pr = parse_rts(std::span<const uint8_t>(bytes, len));
    if (!pr) return;
    const rts_out& r = *pr;
    if (r.leaf_id != _cfg.leaf_id) return;
    if (r.next != _node_id) return;                      // not addressed to us as next-hop
    { EventField f[] = { { .key = "from", .type = EventField::T::i64, .i = r.src },
                         { .key = "dst",  .type = EventField::T::i64, .i = r.dst } };
      _hal.emit("rts_rx", f, 2); }

    // last_acked dedup: a retried RTS after we already delivered -> CTS already_received, no re-deliver.
    const uint32_t lakey = (uint32_t(r.src) << 24) | (uint32_t(r.dst) << 16) |
                           (uint32_t(r.ctr_lo) << 8) | r.payload_len;
    auto la = _last_acked_from.find(lakey);
    if (la != _last_acked_from.end() && (_hal.now() - la->second.t_ms) < protocol::last_acked_ttl_ms) {
        // Fresh within the 10s TTL (dv_dual_sf.lua:9861) — the TTL gate is what stops a
        // stale 4-bit ctr_lo alias from false-positiving on slow sustained traffic.
        cts_in cin{}; cin.ctr_lo = r.ctr_lo; cin.chosen_data_sf = la->second.chosen_data_sf;
        cin.already_received = true; cin.to = r.src;
        uint8_t cbuf[3]; const size_t cl = pack_cts(cin, std::span<uint8_t>(cbuf, 3));
        TxParams cp; cp.sf = static_cast<int16_t>(_cfg.routing_sf); cp.label = "CTS";
        _hal.tx(cbuf, cl, cp);
        EventField f[] = { { .key = "to", .type = EventField::T::i64, .i = r.src },
                           { .key = "dup", .type = EventField::T::boolean, .b = true } };
        _hal.emit("cts_tx", f, 2);
        return;
    }
    // A retried RTS for the SAME flight while we still await its DATA -> re-CTS + restart
    // the expiry (dv_dual_sf.lua:218 CTS-dup) so the sender's retry gets a fresh CTS.
    if (_pending_rx && _pending_rx->from == r.src && _pending_rx->dst == r.dst &&
        _pending_rx->ctr_lo == r.ctr_lo) {
        cts_in cin{}; cin.ctr_lo = r.ctr_lo; cin.chosen_data_sf = _pending_rx->chosen_data_sf;
        cin.already_received = false; cin.to = r.src;
        uint8_t cbuf[3]; const size_t cl = pack_cts(cin, std::span<uint8_t>(cbuf, 3));
        TxParams cp; cp.sf = static_cast<int16_t>(_cfg.routing_sf); cp.label = "CTS";
        _hal.tx(cbuf, cl, cp);
        EventField f[] = { { .key = "to", .type = EventField::T::i64, .i = r.src },
                           { .key = "dup", .type = EventField::T::boolean, .b = true } };
        _hal.emit("cts_tx", f, 2);
        start_pending_rx_expiry(_pending_rx->payload_len);
        return;
    }
    // Busy with a DIFFERENT flight. If we hold a pending_rx (receiving someone else's
    // DATA), NACK the sender with how-long-busy so it waits/requeues instead of
    // grinding rts_timeout (dv:9934). If we hold a pending_tx (sending our own), STAY
    // SILENT (dv:9962 — the busy_for estimate lied for ACK-loss-stuck nodes).
    if (_pending_rx) {
        const uint64_t now = _hal.now();
        uint64_t busy_for = (_pending_rx->expiry_ms > now) ? (_pending_rx->expiry_ms - now) : 0;
        if (busy_for > 65535) busy_for = 65535;
        const uint32_t q = (static_cast<uint32_t>(busy_for) + protocol::nack_busy_quantum_ms - 1)
                           / protocol::nack_busy_quantum_ms;                    // ceil
        nack_in nin{}; nin.reason = protocol::nack_reason_busy_rx; nin.ctr_lo = r.ctr_lo;
        nin.payload = static_cast<uint8_t>(q > 255 ? 255 : q); nin.to = r.src;
        uint8_t nbuf[4]; const size_t nl = pack_nack(nin, std::span<uint8_t>(nbuf, 4));
        TxParams np; np.sf = static_cast<int16_t>(_cfg.routing_sf); np.label = "NACK";
        _hal.tx(nbuf, nl, np);
        EventField f[] = { { .key = "to",      .type = EventField::T::i64, .i = r.src },
                           { .key = "reason",  .type = EventField::T::i64, .i = protocol::nack_reason_busy_rx },
                           { .key = "busy_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(busy_for) } };
        _hal.emit("nack_tx", f, 3);
        return;
    }
    if (_pending_tx) {                                   // sending our own -> silent (no NACK)
        EventField f[] = { { .key = "from", .type = EventField::T::i64, .i = r.src } };
        _hal.emit("rts_drop_pending_tx", f, 1);
        return;
    }

    const uint8_t sf = select_data_sf(r.sf_index);
    PendingRx prx{}; prx.from = r.src; prx.dst = r.dst; prx.ctr_lo = r.ctr_lo;
    prx.chosen_data_sf = sf; prx.payload_len = r.payload_len; prx.set_at_ms = _hal.now();
    _pending_rx = prx;
    start_pending_rx_expiry(r.payload_len);
    cts_in cin{}; cin.ctr_lo = r.ctr_lo; cin.chosen_data_sf = sf; cin.already_received = false; cin.to = r.src;
    uint8_t cbuf[3]; const size_t cl = pack_cts(cin, std::span<uint8_t>(cbuf, 3));
    TxParams cp; cp.sf = static_cast<int16_t>(_cfg.routing_sf); cp.label = "CTS";
    _hal.tx(cbuf, cl, cp);                               // CTS on routing_sf
    { EventField f[] = { { .key = "to", .type = EventField::T::i64, .i = r.src },
                         { .key = "sf", .type = EventField::T::i64, .i = sf } };
      _hal.emit("cts_tx", f, 2); }
    _hal.set_rx_sf(sf);                                  // NOW retune RX to hear the DATA on the data SF
}

void Node::handle_cts(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pc = parse_cts(std::span<const uint8_t>(bytes, len));
    if (!pc) return;
    const cts_out& c = *pc;
    if (c.to != _node_id) return;
    if (!_pending_tx || !_pending_tx->awaiting_cts || _pending_tx->ctr_lo != c.ctr_lo) return;
    if (static_cast<uint8_t>(meta.src_hint) != _pending_tx->next) return;
    _hal.cancel(kRtsTimeoutTimerId);                     // else it fires same-tick and burns a retry
    _hal.cancel(kRetryBackoffTimerId);                   // drop a stale retry armed by a just-fired rts_timeout
    _pending_tx->awaiting_cts = false;
    _pending_tx->chosen_data_sf = c.chosen_data_sf;
    { EventField f[] = { { .key = "from", .type = EventField::T::i64, .i = static_cast<uint8_t>(meta.src_hint) },
                         { .key = "sf",   .type = EventField::T::i64, .i = c.chosen_data_sf } };
      _hal.emit("cts_rx", f, 2); }
    if (c.already_received) { _pending_tx.reset(); become_free(); return; }   // already delivered upstream
    (void)_hal.after(protocol::cts_to_data_gap_ms, kCtsToDataGapTimerId);     // fixed 5ms gap (NOT rand)
}

void Node::do_data_tx() {
    if (!_pending_tx || _pending_tx->awaiting_ack || _pending_tx->chosen_data_sf == 0) return;
    PendingTx& pt = *_pending_tx;
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in din{};
    din.addr_len = 0; din.flags = pt.flags; din.next = pt.next; din.dst = pt.dst;
    din.hops_remaining = protocol::hop_budget_max_initial; din.committed_hops = 0;
    din.prev_fwd_rt_hops = 0; din.ctr = pt.ctr;
    din.visited = {};                                    // empty -> 6 zero bytes
    din.inner = std::span<const uint8_t>(pt.inner, pt.inner_len);
    din.mac   = std::span<const uint8_t>(mac, 4);
    uint8_t buf[protocol::lora_max_frame_bytes];
    const size_t dlen = pack_data(din, std::span<uint8_t>(buf, sizeof(buf)));
    if (dlen == 0) { _hal.log("DATA pack failed"); return; }
    TxParams p; p.sf = static_cast<int16_t>(pt.chosen_data_sf); p.label = "DATA";
    _hal.tx(buf, dlen, p);                               // DATA on the chosen data SF; RX stays routing_sf
    EventField f[] = {
        { .key = "dst",  .type = EventField::T::i64, .i = pt.dst },
        { .key = "next", .type = EventField::T::i64, .i = pt.next },
        { .key = "ctr",  .type = EventField::T::i64, .i = pt.ctr },
    };
    _hal.emit("data_tx", f, 3);
    pt.awaiting_ack = true;
    start_ack_timeout();
}

void Node::handle_data(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pd = parse_data(std::span<const uint8_t>(bytes, len));
    if (!pd) return;
    const data_out& d = *pd;
    if (d.next != _node_id) return;
    if (!_pending_rx || _pending_rx->ctr_lo != d.ctr_lo4) return;
    const uint8_t from = static_cast<uint8_t>(meta.src_hint);
    { EventField f[] = { { .key = "from", .type = EventField::T::i64, .i = from },
                         { .key = "dst",  .type = EventField::T::i64, .i = d.dst } };
      _hal.emit("data_rx", f, 2); }
    const uint8_t rx_sf = _pending_rx->chosen_data_sf;
    const uint8_t pl    = _pending_rx->payload_len;
    _hal.cancel(kPendingRxExpiryTimerId);
    _hal.set_rx_sf(_cfg.routing_sf);                     // receiver retunes back
    _pending_rx.reset();
    // last_acked cache: a retried RTS gets CTS already_received=1 instead of re-delivery.
    const uint32_t lakey = (uint32_t(from) << 24) | (uint32_t(d.dst) << 16) |
                           (uint32_t(d.ctr_lo4) << 8) | pl;
    const uint64_t nowm = _hal.now();
    for (auto it = _last_acked_from.begin(); it != _last_acked_from.end(); )   // prune expired (10s TTL)
        { if ((nowm - it->second.t_ms) >= protocol::last_acked_ttl_ms) it = _last_acked_from.erase(it); else ++it; }
    if (_last_acked_from.size() < protocol::cap_seen_origins)                  // bounded (reuse the 256 cap)
        _last_acked_from[lakey] = LastAcked{ rx_sf, nowm };
    // origin from the DATA inner; seen-origin dedup. Computed BEFORE the ACK so a
    // LOOP_DUP NACKs INSTEAD of re-ACKing.
    auto inner = data_inner(std::span<const uint8_t>(bytes, len), d);
    auto ui = parse_unicast_inner(inner);
    const uint8_t origin = ui ? ui->origin : from;
    const uint32_t sokey = (uint32_t(origin) << 24) | (uint32_t(d.dst) << 16) | d.ctr;
    auto so = _seen_origins.find(sokey);
    const bool live_dup = (so != _seen_origins.end() && so->second > nowm);
    if (live_dup) {
        auto sof = _seen_origin_from.find(sokey);
        if (sof != _seen_origin_from.end() && sof->second != from) {
            // LOOP_DUP: the SAME flight arrived via a DIFFERENT prev-hop (a mesh loop,
            // dv:10971). NACK the sender so it cascades to an alt, and do NOT ACK (the
            // ACK would clear its pending_tx early). prior_from = the first prev-hop.
            nack_in nin{}; nin.reason = protocol::nack_reason_loop_dup; nin.ctr_lo = d.ctr_lo4;
            nin.payload = sof->second; nin.to = from;
            uint8_t nbuf[4]; const size_t nl = pack_nack(nin, std::span<uint8_t>(nbuf, 4));
            TxParams np; np.sf = static_cast<int16_t>(_cfg.routing_sf); np.label = "NACK";
            _hal.tx(nbuf, nl, np);
            EventField nf[] = { { .key = "to",     .type = EventField::T::i64, .i = from },
                                { .key = "reason", .type = EventField::T::i64, .i = protocol::nack_reason_loop_dup },
                                { .key = "ctr",    .type = EventField::T::i64, .i = d.ctr } };
            _hal.emit("nack_tx", nf, 3);
            { EventField df[] = { { .key = "origin", .type = EventField::T::i64, .i = origin },
                                  { .key = "dst",    .type = EventField::T::i64, .i = d.dst },
                                  { .key = "ctr",    .type = EventField::T::i64, .i = d.ctr } };
              _hal.emit("dup_drop", df, 3); }
            become_free();
            return;
        }
    }
    // ACK on routing_sf (2-bit SNR bucket; budget_hint=0 in R3). Fires for a fresh DATA
    // and for a same-prev-hop dup (the lost-ACK re-ACK recovery).
    ack_in ain{}; ain.ctr_lo = d.ctr_lo4; ain.budget_hint = 0;
    ain.snr_bucket = bucket_of_snr_2b(protocol::db_to_q4(meta.snr_db)); ain.to = from;
    uint8_t abuf[3]; const size_t al = pack_ack(ain, std::span<uint8_t>(abuf, 3));
    TxParams ap; ap.sf = static_cast<int16_t>(_cfg.routing_sf); ap.label = "ACK";
    _hal.tx(abuf, al, ap);
    { EventField f[] = { { .key = "to",  .type = EventField::T::i64, .i = from },
                         { .key = "ctr", .type = EventField::T::i64, .i = d.ctr } };
      _hal.emit("ack_tx", f, 2); }
    if (live_dup) { become_free(); return; }                                        // same prev-hop dup -> ACK only
    for (auto it = _seen_origins.begin(); it != _seen_origins.end(); )              // prune expired (30s TTL)
        { if (it->second <= nowm) { _seen_origin_from.erase(it->first); it = _seen_origins.erase(it); } else ++it; }
    if (_seen_origins.size() < protocol::cap_seen_origins) {                        // bounded (256)
        _seen_origins[sokey] = nowm + protocol::seen_origin_ttl_ms;
        _seen_origin_from[sokey] = from;                                            // the prev-hop (LOOP_DUP discriminator)
    }
    // defer deliver/forward by the ACK airtime so it doesn't share a sim step with the ACK.
    _post_ack = PostAck{};
    _post_ack.pending = true; _post_ack.is_forward = (d.dst != _node_id);
    _post_ack.origin = origin; _post_ack.dst = d.dst; _post_ack.ctr_lo = d.ctr_lo4;
    _post_ack.ctr = d.ctr; _post_ack.flags = d.flags; _post_ack.previous_hop = from;
    _post_ack.inner_len = static_cast<uint8_t>(inner.size() <= protocol::max_payload_bytes_hard_cap
                                               ? inner.size() : protocol::max_payload_bytes_hard_cap);
    for (uint8_t i = 0; i < _post_ack.inner_len; ++i) _post_ack.inner[i] = inner[i];
    (void)_hal.after(airtime_routing_ms(3) + 1, kPostAckTimerId);
}

void Node::do_post_ack() {
    if (!_post_ack.pending) return;
    const PostAck pa = _post_ack;
    _post_ack.pending = false;
    if (!pa.is_forward) {
        // deliver: body = inner[2..] (skip src_addr_len + origin), null-terminated for the event.
        char body[protocol::max_payload_bytes_hard_cap + 1];
        const uint8_t blen = (pa.inner_len > 2) ? static_cast<uint8_t>(pa.inner_len - 2) : 0;
        for (uint8_t i = 0; i < blen; ++i) body[i] = static_cast<char>(pa.inner[2 + i]);
        body[blen] = '\0';
        EventField f[] = {
            { .key = "origin",  .type = EventField::T::i64, .i = pa.origin },
            { .key = "dst",     .type = EventField::T::i64, .i = pa.dst },
            { .key = "ctr",     .type = EventField::T::i64, .i = pa.ctr },
            { .key = "payload", .type = EventField::T::str, .s = body },     // dm_delivery keys (dst, payload)
        };
        _hal.emit("delivered", f, 4);
        Push pu{}; pu.kind = PushKind::msg_recv; pu.origin = pa.origin; pu.dst = pa.dst; pu.ctr = pa.ctr;
        pu.body_len = blen; for (uint8_t i = 0; i < blen; ++i) pu.body[i] = static_cast<uint8_t>(body[i]);
        enqueue_push(pu);                                // app channel: the inbound message
        become_free();
    } else {
        TxItem it{};
        it.origin = pa.origin; it.dst = pa.dst; it.ctr = pa.ctr; it.ctr_lo = pa.ctr_lo;
        it.flags = pa.flags; it.is_forward = true; it.previous_hop = pa.previous_hop;
        it.inner_len = pa.inner_len;
        for (uint8_t i = 0; i < pa.inner_len; ++i) it.inner[i] = pa.inner[i];
        it.enqueue_time_ms = _hal.now();                 // fresh hop attempt (dv:11391): the cascade-requeue
                                                         // total-age window starts when THIS hop accepts the
                                                         // forward — else it defaults 0 and the cap mis-fires.
        if (_tx_queue_n < kTxQueueCap) _tx_queue[_tx_queue_n++] = it;
        become_free();
    }
}

void Node::handle_ack(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pk = parse_ack(std::span<const uint8_t>(bytes, len));
    if (!pk) return;
    const ack_out& k = *pk;
    if (k.to != _node_id) return;
    if (!_pending_tx || !_pending_tx->awaiting_ack || _pending_tx->ctr_lo != k.ctr_lo) return;
    if (static_cast<uint8_t>(meta.src_hint) != _pending_tx->next) return;
    _hal.cancel(kAckTimeoutTimerId);
    _hal.cancel(kRetryBackoffTimerId);                   // drop a stale retry armed by a just-fired ack_timeout
    EventField f[] = { { .key = "from", .type = EventField::T::i64, .i = static_cast<uint8_t>(meta.src_hint) },
                       { .key = "ctr",  .type = EventField::T::i64, .i = _pending_tx->ctr } };
    _hal.emit("ack_rx", f, 2);
    { Push pu{}; pu.kind = PushKind::send_acked; pu.dst = _pending_tx->dst; pu.ctr = _pending_tx->ctr; enqueue_push(pu); }
    _pending_tx.reset();
    become_free();
}

// The sender's NACK handler (dv:10365). A NACK is faster feedback than the timeout:
// LOOP_DUP -> cascade to an alt (or direct giveup); BUSY_RX -> mark the peer blind +
// wait-same-hop (short busy) or requeue (long busy). BUDGET/HOP_BUDGET deferred.
void Node::handle_nack(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pn = parse_nack(std::span<const uint8_t>(bytes, len));
    if (!pn) return;
    const nack_out& n = *pn;
    if (n.to != _node_id) return;                                   // not for us
    if (!_pending_tx) return;                                       // no flight to react on
    if (_pending_tx->ctr_lo != n.ctr_lo) return;                    // stale (different flight)
    if (meta.src_hint >= 0 && static_cast<uint8_t>(meta.src_hint) != _pending_tx->next) {
        EventField f[] = { { .key = "from", .type = EventField::T::i64, .i = static_cast<uint8_t>(meta.src_hint) } };
        _hal.emit("nack_drop_unexpected_src", f, 1);
        return;
    }
    _hal.cancel(kRtsTimeoutTimerId);                                // faster than the timeout (dv:10390)
    _hal.cancel(kAckTimeoutTimerId);
    _pending_tx->awaiting_cts = false; _pending_tx->awaiting_ack = false;
    PendingTx& pt = *_pending_tx;

    if (n.reason == protocol::nack_reason_loop_dup) {
        const uint8_t from_next = pt.next;
        mark_tried(pt, pt.next);
        const uint8_t alt = pick_next_cascade_hop(pt);
        if (alt != 0) {                                            // cascade to an alt (NO jitter)
            EventField f[] = { { .key = "origin",   .type = EventField::T::i64, .i = pt.origin },
                               { .key = "dst",      .type = EventField::T::i64, .i = pt.dst },
                               { .key = "ctr",      .type = EventField::T::i64, .i = pt.ctr },
                               { .key = "from_next", .type = EventField::T::i64, .i = from_next },
                               { .key = "next",     .type = EventField::T::i64, .i = alt } };
            _hal.emit("path_cascade", f, 5);
            _hal.emit("tx_loop_alt", f, 5);
            pt.next = alt;
            pt.retries_left = effective_rts_max_retries(pt.requeue_count);
            tx_rts_retry();
        } else {                                                  // LOOP_DUP miss -> DIRECT giveup (NOT requeue, dv:10588)
            EventField f[] = { { .key = "dst", .type = EventField::T::i64, .i = pt.dst },
                               { .key = "ctr", .type = EventField::T::i64, .i = pt.ctr } };
            _hal.emit("path_cascade_exhausted", f, 2);
            _hal.emit("rts_giveup", f, 2);
            { Push pu{}; pu.kind = PushKind::send_failed; pu.dst = pt.dst; pu.ctr = pt.ctr; enqueue_push(pu); }
            _pending_tx.reset();
            become_free();
        }
        return;
    }

    if (n.reason == protocol::nack_reason_busy_rx) {
        const uint64_t now = _hal.now();
        const uint64_t busy_for = static_cast<uint64_t>(n.payload) * protocol::nack_busy_quantum_ms;
        { EventField rf[] = { { .key = "from",   .type = EventField::T::i64, .i = pt.next },
                              { .key = "reason", .type = EventField::T::i64, .i = protocol::nack_reason_busy_rx },
                              { .key = "busy_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(busy_for) } };
          _hal.emit("nack_rx", rf, 3); }
        if (busy_for > 0) {                                        // mark the peer blind, max-merge (dv:10627)
            const uint64_t until = now + busy_for;
            auto bit = _blind_until.find(pt.next);
            _blind_until[pt.next] = (bit != _blind_until.end() && bit->second > until) ? bit->second : until;
            EventField bf[] = { { .key = "next", .type = EventField::T::i64, .i = pt.next } };
            _hal.emit("blind_observed", bf, 1);
        }
        if (busy_for <= protocol::nack_wait_threshold_ms) {        // short busy -> wait SAME hop
            const int jit = _hal.rand_range(0, static_cast<int>(retry_jitter_ms()) + 1);   // N1 (the only new draw)
            const uint32_t wait = static_cast<uint32_t>(busy_for) + 1 + static_cast<uint32_t>(jit);
            _nack_wait_ctr_lo = pt.ctr_lo; _nack_wait_pending = true;
            (void)_hal.after(wait, kNackWaitTimerId);
        } else {                                                  // long busy -> requeue SAME hop (verbatim meta)
            TxItem it{};
            it.origin = pt.origin; it.dst = pt.dst; it.ctr = pt.ctr; it.ctr_lo = pt.ctr_lo; it.flags = pt.flags;
            it.inner_len = pt.inner_len;
            for (uint8_t i = 0; i < pt.inner_len; ++i) it.inner[i] = pt.inner[i];
            it.is_forward = pt.has_previous_hop; it.previous_hop = pt.previous_hop;
            it.requeue_count = pt.requeue_count; it.enqueue_time_ms = pt.enqueue_time_ms;   // VERBATIM (no ++/backoff)
            it.next_attempt_ms = 0;
            EventField tf[] = { { .key = "dst", .type = EventField::T::i64, .i = pt.dst },
                                { .key = "ctr", .type = EventField::T::i64, .i = pt.ctr } };
            _hal.emit("tx_requeued", tf, 2);
            if (_tx_queue_n < kTxQueueCap) _tx_queue[_tx_queue_n++] = it;
            _pending_tx.reset();
            become_free();
        }
        return;
    }

    // BUDGET(1)/HOP_BUDGET(2): reactions DEFERRED (R4 / hop-budget), never emitted this
    // milestone. Defensive: restore awaiting_cts + re-arm so an unexpected NACK doesn't
    // strand the flight (the timeouts were cancelled above).
    pt.awaiting_cts = true;
    start_rts_timeout();
}

void Node::start_rts_timeout() {
    const uint32_t base = airtime_routing_ms(8) + airtime_routing_ms(3);   // Lua RTS_LEN=8 + CTS (timing matches Lua)
    const uint8_t  attempt = static_cast<uint8_t>(protocol::rts_max_retries -
                              (_pending_tx ? _pending_tx->retries_left : 0));
    const uint32_t shift = attempt < 2 ? attempt : 2;                       // x2 backoff, cap x4
    (void)_hal.after((base << shift) + 1, kRtsTimeoutTimerId);
}
void Node::start_ack_timeout() {
    const uint8_t  sf  = _pending_tx ? _pending_tx->chosen_data_sf : _cfg.data_sf;
    const uint16_t len = static_cast<uint16_t>(18 + (_pending_tx ? _pending_tx->inner_len : 0));
    const uint32_t base = airtime_ms(sf, _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym, len)
                        + airtime_routing_ms(3);
    (void)_hal.after(base + 2, kAckTimeoutTimerId);
}
void Node::start_pending_rx_expiry(uint8_t payload_len) {
    const uint8_t  sf  = _pending_rx ? _pending_rx->chosen_data_sf : _cfg.data_sf;
    const uint16_t len = static_cast<uint16_t>(14 + payload_len);
    const uint32_t t = airtime_routing_ms(3) + protocol::cts_to_data_gap_ms +
                       airtime_ms(sf, _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym, len) + 2;
    if (_pending_rx) _pending_rx->expiry_ms = _hal.now() + t;   // for the BUSY_RX NACK busy_for calc
    (void)_hal.after(t, kPendingRxExpiryTimerId);
}

}  // namespace meshroute
