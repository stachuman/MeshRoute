// MeshRoute — remote controller. R-RA-22/45 bound every owned byte; adapters never escape a call.
#include "remote_client.h"
#include "frame_codec.h"
#include "protocol_constants.h"
#include "../console/console_line.h"
#include "monocypher.h"
#include <cstring>
#include <algorithm>

namespace MESHROUTE_NS {
namespace {
constexpr uint8_t kOpcodeMask = 7, kAck = 8, kSealed = 16, kRolled = 32, kRediscovered = 64;
constexpr uint8_t kLocalResult = 4, kRolloverResult = 5;
constexpr uint32_t kOutcomeMs = protocol::e2e_ack_deadline_xl_ms;
// ACK debt may retry; R-RA-44 leaves silent EXECUTE retry timing to 8b.
constexpr uint32_t kAckRetryMs = protocol::cascade_requeue_base_ms;
using Phase = RemoteClientPhase;
void increment(uint16_t& n) { if (n != UINT16_MAX) ++n; }
bool live(const RemoteClientPending& p) { return p.core.state != static_cast<uint8_t>(Phase::empty); }
Phase phase(const RemoteClientPending& p) { return static_cast<Phase>(p.core.state); }
void phase(RemoteClientPending& p, Phase s) { p.core.state = static_cast<uint8_t>(s); }
uint8_t opcode(const RemoteClientPending& p) { return p.core.flags & kOpcodeMask; }
bool authenticated(const RemoteClientPending& p) { return opcode(p) != static_cast<uint8_t>(RemoteCmdOpcode::open_execute); }
bool equal_key(const uint8_t* a, const uint8_t* b) { return crypto_verify32(a, b) == 0; }
bool same_pair(const RemoteClientSession& e, const RemoteClientPending& p) {
    return e.valid && equal_key(e.target_admin_pub, p.core.target_admin_pub) && equal_key(e.controller_pub, p.core.controller_pub);
}
RemoteClientSession* session(RemoteClientState& s, const RemoteClientPending& p) {
    for (auto& e : s.sessions) if (same_pair(e, p)) return &e;
    return nullptr;
}
bool session_busy(const RemoteClientState& s, const RemoteClientSession& e) {
    for (const auto& p : s.pending) if (live(p) && authenticated(p) && same_pair(e, p)) return true;
    return false;
}
RemoteClientPending* pending(RemoteClientState& s, uint64_t id) {
    for (auto& p : s.pending) if (live(p) && p.core.request_id == id) return &p;
    return nullptr;
}
RemoteClientAssembly* assembly(RemoteClientState& s, uint64_t id) {
    for (auto& a : s.assemblies) if (a.in_use && a.request_id == id) return &a;
    return nullptr;
}
RemoteClientRetained* retained(RemoteClientState& s, uint64_t id) {
    for (auto& r : s.retained) if (r.in_use && r.request_id == id) return &r;
    return nullptr;
}
bool id_live(const RemoteClientState& s, uint64_t id) {
    for (const auto& p : s.pending)
        if (live(p) && (p.core.request_id == id || p.core.discovery_id == id)) return true;
    for (const auto& d : s.ack_debt) if (d.in_use && d.request_id == id) return true;
    return false;
}
RemoteClientError new_id(const RemoteClientState& s, uint64_t& id, RemoteEntropyFn entropy, void* ctx) {
    uint64_t candidate = 0;
    if (remote_make_request_id(candidate, entropy, ctx) != RemoteStatus::ok) return RemoteClientError::entropy_failed;
    // The codec admits zero; this controller's explicit local contract reserves it as no request.
    if (!candidate || id_live(s, candidate)) return RemoteClientError::duplicate_id;
    id = candidate;
    return RemoteClientError::none;
}
struct Keys {
    uint8_t session_key[32]{};
    RemoteKeys view{};
    Keys(RemoteClientState& s, const RemoteClientPending& p) {
        if (auto* e = session(s, p)) {
            view.base = e->base_key;
            if (remote_kdf_session(session_key, e->base_key, p.core.admin_epoch) == RemoteStatus::ok)
                view.session = session_key;
        }
    }
    ~Keys() { crypto_wipe(session_key, sizeof session_key); }
};
void free_chain(RemoteClientState& s, uint16_t head) {
    for (unsigned n = 0; head < 8 && n < 8; ++n) {
        const auto next = s.chunks[head].next;
        crypto_wipe(&s.chunks[head], sizeof s.chunks[head]); head = next;
    }
}
bool in_chain(const RemoteClientState& s, uint16_t head, uint16_t wanted) {
    for (unsigned n = 0; head < 8 && n < 8; ++n) {
        if (head == wanted) return true;
        head = s.chunks[head].next;
    }
    return false;
}
bool chunk_used(const RemoteClientState& s, uint16_t i) {
    for (const auto& a : s.assemblies) if (a.in_use && in_chain(s, a.first_chunk, i)) return true;
    for (const auto& r : s.retained) if (r.in_use && in_chain(s, r.first_chunk, i)) return true;
    return false;
}
void release(RemoteClientState& s, RemoteClientPending& p) {
    if (auto* a = assembly(s, p.core.request_id)) { free_chain(s, a->first_chunk); *a = {}; }
    if (auto* r = retained(s, p.core.request_id)) { free_chain(s, r->first_chunk); *r = {}; }
    crypto_wipe(&p, sizeof p);
}
void complete(RemoteClientState& s, RemoteClientPending& p, uint8_t domain, uint8_t code, uint32_t detail, uint32_t now) {
    auto* a = assembly(s, p.core.request_id);
    if (!a) return;
    a->terminal_seen = 1; a->result_domain = domain; a->result_code = code; a->result_detail = detail;
    phase(p, Phase::complete);
    // BLE reserved this header before TX. USB can retain in its assembly if the result pool is occupied.
    auto* r = retained(s, p.core.request_id);
    if (!r) for (auto& row : s.retained) if (!row.in_use) { r = &row; break; }
    if (r) {
        *r = {p.core.request_id, now, a->bytes_used, a->first_chunk, p.core.local_transport,
              0, 1, 2, domain, code, detail};
        *a = {}; // chunk ownership transfers once, without a second copy/free
    }
}
void local_complete(RemoteClientState& s, RemoteClientPending& p, RemoteClientError error, uint32_t now) {
    complete(s, p, kLocalResult, static_cast<uint8_t>(error), 0, now);
}
bool append(RemoteClientState& s, RemoteClientAssembly& a, std::span<const uint8_t> body) {
    uint16_t* tail = &a.first_chunk;
    RemoteClientChunk* last = nullptr;
    for (unsigned n = 0; *tail < 8 && n < 8; ++n) {
        last = &s.chunks[*tail]; tail = &last->next;
    }
    // OPEN frames can exceed one chunk. Pack across frame boundaries so the
    // shared pool holds its full byte budget without wasting each frame's tail.
    const size_t spare = last ? kRemoteClientChunkBytes - last->len : 0;
    const size_t in_tail = std::min(body.size(), spare);
    const size_t remaining = body.size() - in_tail;
    const size_t needed = (remaining + kRemoteClientChunkBytes - 1) / kRemoteClientChunkBytes;
    uint16_t slots[8]{}; size_t count = 0;
    for (uint16_t i = 0; i < 8 && count < needed; ++i) if (!chunk_used(s, i)) slots[count++] = i;
    if (count != needed) return false; // no partial append, no sequence advance
    size_t off = in_tail;
    if (in_tail) {
        std::memcpy(last->bytes + last->len, body.data(), in_tail);
        last->len += static_cast<uint16_t>(in_tail);
    }
    for (size_t i = 0; i < count; ++i) {
        auto& c = s.chunks[slots[i]];
        c.len = static_cast<uint16_t>(std::min(body.size() - off, kRemoteClientChunkBytes));
        if (c.len) std::memcpy(c.bytes, body.data() + off, c.len);
        c.next = kRemoteClientNoChunk; *tail = slots[i]; tail = &c.next; off += c.len;
    }
    a.bytes_used += static_cast<uint32_t>(body.size());
    return true;
}
RemoteStatus seal_request(RemoteClientState& s, RemoteClientPending& p, std::span<const uint8_t> command) {
    Keys keys(s, p); RemoteMessage m{};
    m.outer_type = DATA_TYPE_REMOTE_CMD; m.opcode = opcode(p); m.slot = p.core.acl_slot; m.request_id = p.core.request_id;
    uint8_t bytes[kRemoteClientSealedBytes]{}; size_t len = 0;
    const auto st = remote_body_encode(bytes, len, m, command, keys.view, {true, p.core.source_hash},
                                      remote_client_carrier(p.core.route, DATA_TYPE_REMOTE_CMD, p.core.carrier));
    if (st == RemoteStatus::ok) {
        crypto_wipe(p.sealed, sizeof p.sealed); std::memcpy(p.sealed, bytes, len);
        p.core.sealed_len = static_cast<uint16_t>(len); p.core.flags |= kSealed;
    }
    crypto_wipe(bytes, sizeof bytes); return st;
}
RemoteClientError send_ready(RemoteClientState& s, RemoteClientPending& p, IRadminCarrier& carrier) {
    auto state = phase(p);
    if (state != Phase::bootstrap_ready && state != Phase::compare_ready &&
        state != Phase::rollover_ready && state != Phase::request_ready) return RemoteClientError::none;
    if (carrier.tx_queue_full()) { increment(s.counters.radio_enqueue_failure); return RemoteClientError::radio_enqueue_failed; }
    uint8_t control[kRemoteOverheadBootstrapRequest]{}; size_t len = 0;
    std::span<const uint8_t> bytes;
    const auto leg = remote_client_carrier(p.core.route, DATA_TYPE_REMOTE_CMD, p.core.carrier);
    if (state == Phase::request_ready) bytes = {p.sealed, p.core.sealed_len};
    else {
        RemoteMessage m{}; m.outer_type = DATA_TYPE_REMOTE_CMD; m.request_id = p.core.discovery_id;
        m.opcode = static_cast<uint8_t>(state == Phase::rollover_ready ? RemoteCmdOpcode::safe_rollover : RemoteCmdOpcode::bootstrap);
        m.slot = state == Phase::rollover_ready ? p.core.acl_slot : kRemoteSlotSentinel;
        if (state != Phase::rollover_ready) m.controller_pub = p.core.controller_pub;
        Keys keys(s, p);
        if (remote_body_encode(control, len, m, {}, keys.view, {true, p.core.source_hash}, leg) != RemoteStatus::ok)
            return RemoteClientError::authentication_failed;
        bytes = {control, len};
    }
    const auto sent = carrier.submit_request(p.core.route, {true, p.core.source_hash}, leg, bytes,
                                            state == Phase::request_ready && (p.core.flags & kAck));
    crypto_wipe(control, sizeof control);
    if (sent != RemoteClientSend::queued) {
        increment(s.counters.radio_enqueue_failure);
        return sent == RemoteClientSend::unavailable ? RemoteClientError::carrier_unavailable : RemoteClientError::radio_enqueue_failed;
    }
    switch (state) {
        case Phase::bootstrap_ready: phase(p, Phase::bootstrap_wait); break;
        case Phase::compare_ready: phase(p, Phase::compare_wait); break;
        case Phase::rollover_ready: phase(p, Phase::rollover_wait); break;
        case Phase::request_ready: phase(p, Phase::response_wait); break;
        default: break; // state was checked above; every other phase sends nothing
    }
    return RemoteClientError::none;
}
void epoch_update(RemoteClientState& s, RemoteClientPending& p, uint64_t epoch, uint8_t slot, uint32_t now) {
    auto* e = session(s, p); if (!e) return;
    if (e->valid == 2 && e->admin_epoch != epoch) {
        // The authenticated generation change, not a submitted ACK, proves these old debts are obsolete.
        for (auto& d : s.ack_debt) if (d.in_use && d.credential_slot == p.core.credential_slot &&
            equal_key(d.target_admin_pub, p.core.target_admin_pub)) crypto_wipe(&d, sizeof d);
    }
    e->admin_epoch = epoch; e->acl_slot = slot; e->last_used_ms = now; e->valid = 2;
    (void)remote_kdf_session(e->session_key, e->base_key, epoch);
}
bool result(RemoteClientState& s, RemoteClientPending& p, uint16_t& head, uint8_t& domain, uint8_t& code, uint32_t& detail) {
    if (phase(p) != Phase::complete) return false;
    if (auto* r = retained(s, p.core.request_id)) {
        if (r->in_use != 2) return false;
        head = r->first_chunk; domain = r->result_domain; code = r->result_code; detail = r->result_detail; return true;
    }
    if (auto* a = assembly(s, p.core.request_id)) {
        if (!a->terminal_seen) return false;
        head = a->first_chunk; domain = a->result_domain; code = a->result_code; detail = a->result_detail; return true;
    }
    return false;
}
const char* result_name(uint8_t domain, uint8_t code) {
    if (domain == kLocalResult) return remote_client_error_name(static_cast<RemoteClientError>(code));
    if (domain == kRolloverResult) return "rollover_completed";
    if (domain == static_cast<uint8_t>(RemoteResultKind::protocol_error)) return "already_acknowledged";
    if (domain == static_cast<uint8_t>(RemoteResultKind::admission)) {
        switch (static_cast<RemoteAdmission>(code)) {
            case RemoteAdmission::session_full: return "session_full";
            case RemoteAdmission::ingress_full: return "ingress_full";
            case RemoteAdmission::session_busy: return "session_busy";
            case RemoteAdmission::executing: return "executing";
            case RemoteAdmission::preparation_failed: return "preparation_failed";
        }
    }
    switch (static_cast<RemoteTerminal>(code)) {
        case RemoteTerminal::completed: return "completed";
        case RemoteTerminal::scheduled: return "scheduled";
        case RemoteTerminal::unknown_command: return "unknown_command";
        case RemoteTerminal::refused: return "refused";
        case RemoteTerminal::output_truncated: return "output_truncated";
        case RemoteTerminal::internal_error: return "internal_error";
        case RemoteTerminal::session_full: return "session_full";
        case RemoteTerminal::session_busy: return "session_busy";
        case RemoteTerminal::action_busy: return "action_busy";
    }
    return "internal_error";
}
RemoteClientError accept_result(RemoteClientState& s, RemoteClientPending& p, uint32_t now) {
    uint16_t head; uint8_t domain, code; uint32_t detail;
    if (!result(s, p, head, domain, code, detail)) return RemoteClientError::incomplete;
    // Only a complete authenticated TERMINAL owes a target transcript ACK.
    auto* e = session(s, p);
    if (opcode(p) == static_cast<uint8_t>(RemoteCmdOpcode::auth_execute) && domain == static_cast<uint8_t>(RemoteResultKind::terminal) && e && e->admin_epoch == p.core.admin_epoch) {
        RemoteClientAck* d = nullptr;
        for (auto& row : s.ack_debt) if (!row.in_use) { d = &row; break; }
        if (!d) { increment(s.counters.local_result_pressure); return RemoteClientError::ack_debt_full; }
        RemoteMessage m{}; m.outer_type = DATA_TYPE_REMOTE_CMD; m.opcode = static_cast<uint8_t>(RemoteCmdOpcode::response_ack);
        m.slot = p.core.acl_slot; m.request_id = p.core.request_id;
        Keys keys(s, p); size_t len = 0; uint8_t bytes[kRemoteOverheadSessionControl]{};
        const auto st = remote_body_encode(bytes, len, m, {}, keys.view, {true, p.core.source_hash},
                                          remote_client_carrier(p.core.route, DATA_TYPE_REMOTE_CMD, p.core.carrier));
        if (st != RemoteStatus::ok || len != sizeof bytes) return RemoteClientError::authentication_failed;
        *d = {}; d->request_id = p.core.request_id; d->next_retry_ms = now;
        std::memcpy(d->target_admin_pub, p.core.target_admin_pub, 32); std::memcpy(d->sealed_ack, bytes, sizeof bytes);
        d->acl_slot = p.core.acl_slot; d->in_use = 1; d->source_hash = p.core.source_hash;
        d->route = p.core.route; d->credential_slot = p.core.credential_slot; d->target_book_slot = p.core.target_book_slot;
        d->carrier = p.core.carrier; crypto_wipe(bytes, sizeof bytes);
    }
    release(s, p); return RemoteClientError::none;
}
RemoteClientError deliver(RemoteClientState& s, RemoteClientPending& p, RemoteLocalTransport transport,
                          uint32_t now, IRemoteLocalDelivery& out) {
    uint16_t head; uint8_t domain, code; uint32_t detail;
    if (!result(s, p, head, domain, code, detail)) return RemoteClientError::incomplete;
    // An explicit show adopts its caller's transport for subsequent reconnect re-offers.
    p.core.local_transport = static_cast<uint8_t>(transport);
    if (auto* r = retained(s, p.core.request_id)) r->local_transport = p.core.local_transport;
    if (!out.connected(transport)) return RemoteClientError::incomplete;
    out.begin_result();
    uint16_t seq = 0; bool ok = true;
    for (unsigned n = 0; head < 8 && n < 8; ++n) {
        auto& c = s.chunks[head];
        if (!out.output(transport, p.core.request_id, seq, {c.bytes, c.len})) { ok = false; break; }
        head = c.next;
    }
    if (ok) ok = out.terminal(transport, p.core.request_id, result_name(domain, code), detail,
                              domain == static_cast<uint8_t>(RemoteResultKind::terminal) && code == static_cast<uint8_t>(RemoteTerminal::scheduled));
    if (auto* r = retained(s, p.core.request_id)) { r->delivered = ok; r->reoffer_from_zero = 0; }
    else if (auto* a = assembly(s, p.core.request_id)) a->reserved = ok ? 2 : 1;
    if (!ok) { increment(s.counters.local_result_pressure); out.retained(transport, p.core.request_id); return RemoteClientError::incomplete; }
    // Unknown is retained for explicit retry/forget. No silent retry and no ACK for an unknown outcome.
    if (transport == RemoteLocalTransport::usb && !(domain == kLocalResult && code == static_cast<uint8_t>(RemoteClientError::unknown)))
        return accept_result(s, p, now);
    return RemoteClientError::none;
}
} // namespace

RemoteStatus remote_client_base(uint8_t out[32], const Identity& self, const uint8_t target_pub[32]) {
    uint8_t peer[32]{}, shared[32]{};
    ed_pub_to_x25519(peer, target_pub);
    auto st = remote_ecdh_shared(shared, self, peer);
    if (st == RemoteStatus::ok) st = remote_kdf_base(out, shared, self.ed_pub, target_pub);
    crypto_wipe(peer, sizeof peer); crypto_wipe(shared, sizeof shared); return st;
}
RemoteCarrier remote_client_carrier(const RemoteClientRoute& route, uint8_t type, uint8_t carrier) {
    RemoteCarrier c{}; c.outer_data_type = type; c.dst_hash_on_wire = true;
    c.cross_layer = route.hop_count != 0;
    c.path_depth = route.hop_count ? static_cast<uint8_t>(route.hop_count + (carrier == 0 ? 1 : 0)) : 0;
    c.path_cursor = 0;
    if (carrier == 1) { c.wrapper = true; c.outer_data_type = DATA_TYPE_MOBILE_SEND; c.enclosed_type = type; }
    return c;
}
RemoteClientResult remote_client_start(RemoteClientState& s, const RemoteClientRequest& in, uint32_t now,
                                      RemoteEntropyFn entropy, void* ctx, IRadminCarrier& carrier) {
    const bool auth = in.opcode != RemoteCmdOpcode::open_execute;
    const bool execute = in.opcode == RemoteCmdOpcode::auth_execute || in.opcode == RemoteCmdOpcode::open_execute;
    if (in.carrier > 1 || in.target_pub.size() != 32 || !in.route.target_hash || in.route.hop_count > 3 || (auth && !in.identity) ||
        (!auth && in.e2e_ack) || (!execute && in.opcode != RemoteCmdOpcode::safe_rollover && in.opcode != RemoteCmdOpcode::force_rollover))
        return {RemoteClientError::bad_args, 0};
    if (execute && (in.command.empty() || meshroute::console::validate_command_line(reinterpret_cast<const char*>(in.command.data()),
        in.command.size(), meshroute::console::remote_command_max_bytes) != meshroute::console::LineErr::ok)) return {RemoteClientError::bad_args, 0};
    if (!execute && !in.command.empty()) return {RemoteClientError::bad_args, 0};
    if (!auth && !((in.command.size() == 6 && !std::memcmp(in.command.data(), "status", 6)) ||
                   (in.command.size() == 6 && !std::memcmp(in.command.data(), "routes", 6)))) return {RemoteClientError::bad_args, 0};
    for (uint8_t i = 0; i < in.route.hop_count; ++i) if (!in.route.hops[i]) return {RemoteClientError::bad_args, 0};
    RemoteClientPending* p = nullptr; RemoteClientAssembly* a = nullptr; RemoteClientRetained* r = nullptr;
    for (auto& row : s.pending) if (!live(row)) { p = &row; break; }
    if (!p) { increment(s.counters.request_table_pressure); return {RemoteClientError::request_table_full, 0}; }
    for (auto& row : s.assemblies) if (!row.in_use) { a = &row; break; }
    if (!a) { increment(s.counters.assembly_failure); return {RemoteClientError::assembly_full, 0}; }
    if (in.transport == RemoteLocalTransport::ble) {
        for (auto& row : s.retained) if (!row.in_use) { r = &row; break; }
        if (!r) { increment(s.counters.local_result_pressure); return {RemoteClientError::result_full, 0}; }
    }
    size_t debt = 0; unsigned correlation = 0;
    for (const auto& d : s.ack_debt) debt += d.in_use != 0;
    for (const auto& row : s.pending) if (live(row)) { debt += opcode(row) == static_cast<uint8_t>(RemoteCmdOpcode::auth_execute); correlation += !!(row.core.flags & kAck); }
    if (in.opcode == RemoteCmdOpcode::auth_execute && debt >= 8) { increment(s.counters.local_result_pressure); return {RemoteClientError::ack_debt_full, 0}; }
    if (in.e2e_ack && correlation >= in.correlation_free) return {RemoteClientError::correlation_full, 0};
    uint64_t id = 0; auto error = new_id(s, id, entropy, ctx);
    if (error != RemoteClientError::none) return {error, 0};
    RemoteClientSession* e = nullptr; bool created = false;
    if (auth) {
        for (auto& row : s.sessions) if (row.valid && equal_key(row.target_admin_pub, in.target_pub.data()) && equal_key(row.controller_pub, in.identity->ed_pub)) { e = &row; break; }
        if (!e) {
            for (auto& row : s.sessions) if (!row.valid) { e = &row; break; }
            if (!e) for (auto& row : s.sessions) if (!session_busy(s, row)) { e = &row; break; }
            if (!e) return {RemoteClientError::session_cache_full, 0};
            uint8_t base[32]{};
            if (remote_client_base(base, *in.identity, in.target_pub.data()) != RemoteStatus::ok) {
                increment(s.counters.auth_failure); return {RemoteClientError::authentication_failed, 0};
            }
            crypto_wipe(e, sizeof *e); e->valid = 1; e->last_used_ms = now;
            std::memcpy(e->target_admin_pub, in.target_pub.data(), 32); std::memcpy(e->controller_pub, in.identity->ed_pub, 32);
            std::memcpy(e->base_key, base, 32); crypto_wipe(base, sizeof base);
            e->credential_slot = in.credential_slot; e->target_book_slot = in.target_book_slot; created = true;
        }
    }
    *p = {}; p->core.request_id = id; p->core.source_hash = in.source_hash; p->core.outcome_deadline_ms = now + kOutcomeMs;
    p->core.local_transport = static_cast<uint8_t>(in.transport); p->core.route = in.route; p->core.carrier = in.carrier;
    p->core.credential_slot = in.credential_slot; p->core.target_book_slot = in.target_book_slot;
    p->core.flags = static_cast<uint8_t>(in.opcode) | (in.e2e_ack ? kAck : 0);
    p->core.acl_slot = kRemoteSlotSentinel;
    std::memcpy(p->core.target_admin_pub, in.target_pub.data(), 32);
    if (auth) std::memcpy(p->core.controller_pub, in.identity->ed_pub, 32);
    *a = {}; a->request_id = id; a->started_ms = now; a->first_chunk = kRemoteClientNoChunk; a->in_use = 1;
    if (r) { *r = {}; r->request_id = id; r->first_chunk = kRemoteClientNoChunk; r->in_use = 1; r->local_transport = p->core.local_transport; }
    phase(*p, Phase::request_ready);
    if (auth && e->valid != 2) {
        p->core.sealed_len = static_cast<uint16_t>(in.command.size());
        if (!in.command.empty()) std::memcpy(p->sealed, in.command.data(), in.command.size());
        error = new_id(s, p->core.discovery_id, entropy, ctx);
        phase(*p, Phase::bootstrap_ready);
    } else {
        if (auth) { p->core.admin_epoch = e->admin_epoch; p->core.acl_slot = e->acl_slot; }
        if (seal_request(s, *p, in.command) != RemoteStatus::ok) error = RemoteClientError::bad_args;
    }
    if (error == RemoteClientError::none) error = send_ready(s, *p, carrier);
    if (error != RemoteClientError::none) {
        release(s, *p); if (created) crypto_wipe(e, sizeof *e);
        return {error, 0};
    }
    return {RemoteClientError::none, id};
}

RemoteClientError remote_client_retry(RemoteClientState& s, uint64_t id, uint32_t now, RemoteEntropyFn entropy, void* ctx) {
    auto* p = pending(s, id);
    if (!p) return RemoteClientError::not_found;
    if (!authenticated(*p) || !(p->core.flags & kSealed) ||
        (phase(*p) != Phase::response_wait && phase(*p) != Phase::complete)) return RemoteClientError::bad_args;
    uint16_t head; uint8_t domain, code; uint32_t detail;
    if (phase(*p) == Phase::complete && (!result(s,*p,head,domain,code,detail) || domain != kLocalResult ||
        code != static_cast<uint8_t>(RemoteClientError::unknown))) return RemoteClientError::bad_args;
    auto* a = assembly(s,id);
    if (!a) for (auto& row:s.assemblies) if (!row.in_use) { a=&row; break; }
    if (!a) { increment(s.counters.assembly_failure); return RemoteClientError::assembly_full; }
    uint64_t discovery = 0;
    const auto error = new_id(s,discovery,entropy,ctx);
    if (error != RemoteClientError::none) return error;
    // Transfer retained bytes back to the assembly during comparison. Changed epoch preserves that output;
    // an equal epoch authorizes replacing it with the target's replay, starting at sequence zero.
    if (auto* r=retained(s,id); r && r->in_use==2) {
        *a={}; a->in_use=1; a->request_id=id; a->started_ms=now;
        a->first_chunk=r->first_chunk; a->bytes_used=r->bytes_total;
        r->first_chunk=kRemoteClientNoChunk; r->bytes_total=0; r->in_use=1; r->delivered=0;
    }
    a->terminal_seen=0; a->reserved=0;
    p->core.discovery_id=discovery; p->core.outcome_deadline_ms=now+kOutcomeMs;
    phase(*p,Phase::compare_ready);
    return RemoteClientError::none;
}

void remote_client_receive(RemoteClientState& s, uint8_t type, std::span<const uint8_t> body,
                           RemoteSource sender, const RemoteCarrier& carrier, uint32_t now) {
    if (type != DATA_TYPE_REMOTE_RESP || body.size() < 9 || !sender.present) { increment(s.counters.unmatched_response); return; }
    uint64_t id = 0; for (unsigned i = 0; i < 8; ++i) id |= uint64_t{body[1+i]} << (8*i);
    RemoteClientPending* p = nullptr;
    for (auto& row : s.pending) if (live(row) && (row.core.request_id == id ||
        ((phase(row) == Phase::bootstrap_wait || phase(row) == Phase::compare_wait || phase(row) == Phase::rollover_wait) && row.core.discovery_id == id))) { p = &row; break; }
    if (!p || sender.hash != p->core.route.target_hash) { increment(s.counters.unmatched_response); return; }
    Keys keys(s, *p); RemoteDecoded d{}; uint8_t plaintext[kRemoteClientSealedBytes]{};
    const auto status = remote_body_decode(d, type, body, keys.view, {true, p->core.source_hash}, carrier, plaintext);
    if (status != RemoteStatus::ok || d.authenticated != authenticated(*p)) {
        crypto_wipe(plaintext, sizeof plaintext); increment(s.counters.auth_failure);
        if (phase(*p) == Phase::response_wait && authenticated(*p) && !(p->core.flags & kRediscovered)) {
            p->core.flags |= kRediscovered; phase(*p, Phase::compare_ready); p->core.discovery_id = 0;
        } else if (authenticated(*p) && (p->core.flags & kRediscovered)) {
            local_complete(s,*p,RemoteClientError::authentication_failed,now);
        }
        return;
    }
    const auto state = phase(*p);
    const bool discovery = state == Phase::bootstrap_wait || state == Phase::compare_wait;
    if (discovery || state == Phase::rollover_wait) {
        const auto expected = discovery ? RemoteRespOpcode::bootstrap : RemoteRespOpcode::rollover_result;
        if (d.msg.opcode == static_cast<uint8_t>(expected) && id == p->core.discovery_id) {
            if (state == Phase::rollover_wait && d.msg.slot != p->core.acl_slot) {
                increment(s.counters.auth_failure); crypto_wipe(plaintext,sizeof plaintext); return;
            }
            if (state == Phase::compare_wait && (d.msg.admin_epoch != p->core.admin_epoch || d.msg.slot != p->core.acl_slot)) {
                epoch_update(s, *p, d.msg.admin_epoch, d.msg.slot, now);
                if (!assembly(s, p->core.request_id)) {
                    for (auto& a : s.assemblies) if (!a.in_use) { a = {}; a.in_use = 1; a.request_id = p->core.request_id; a.first_chunk = kRemoteClientNoChunk; break; }
                }
                local_complete(s, *p, RemoteClientError::unknown, now);
            } else if (state == Phase::compare_wait) {
                auto* a = assembly(s, p->core.request_id);
                if (!a) for (auto& row : s.assemblies) if (!row.in_use) { a = &row; break; }
                if (!a) { increment(s.counters.assembly_failure); crypto_wipe(plaintext, sizeof plaintext); return; }
                if (auto* r = retained(s, p->core.request_id)) { free_chain(s, r->first_chunk); r->first_chunk = kRemoteClientNoChunk; r->in_use = 1; r->delivered = 0; }
                if (a->in_use) free_chain(s, a->first_chunk);
                *a = {}; a->in_use = 1; a->request_id = p->core.request_id; a->first_chunk = kRemoteClientNoChunk; a->started_ms = now;
                phase(*p, Phase::request_ready); p->core.outcome_deadline_ms = now + kOutcomeMs;
            } else if (state == Phase::bootstrap_wait) {
                epoch_update(s, *p, d.msg.admin_epoch, d.msg.slot, now); p->core.admin_epoch = d.msg.admin_epoch; p->core.acl_slot = d.msg.slot;
                if (seal_request(s, *p, {p->sealed, p->core.sealed_len}) == RemoteStatus::ok) phase(*p, Phase::request_ready);
                else local_complete(s, *p, RemoteClientError::authentication_failed, now);
            } else {
                // Auto-rollover: recover the not-executed command under the OLD captured epoch before changing it.
                RemoteDecoded old{}; uint8_t command[kRemoteClientSealedBytes]{};
                const auto opened = remote_body_decode(old, DATA_TYPE_REMOTE_CMD, {p->sealed,p->core.sealed_len}, keys.view,
                    {true,p->core.source_hash}, remote_client_carrier(p->core.route,DATA_TYPE_REMOTE_CMD,p->core.carrier), command);
                if (opened == RemoteStatus::ok) {
                    epoch_update(s, *p, d.msg.admin_epoch, d.msg.slot, now); p->core.admin_epoch = d.msg.admin_epoch; p->core.acl_slot = d.msg.slot;
                    // Fresh ID and re-sealing happen on the main loop. Only this not-executed command
                    // occupies the same inline row as plaintext until that call.
                    crypto_wipe(p->sealed,sizeof p->sealed); std::memcpy(p->sealed, old.body.data(), old.body.size());
                    p->core.sealed_len = static_cast<uint16_t>(old.body.size()); p->core.flags &= ~kSealed;
                    phase(*p, Phase::request_ready);
                } else local_complete(s, *p, RemoteClientError::authentication_failed, now);
                crypto_wipe(command,sizeof command);
            }
            crypto_wipe(plaintext,sizeof plaintext); return;
        }
        // SAFE_ROLLOVER can instead return a typed admission refusal; handled below.
        if (state != Phase::rollover_wait || d.result_kind != RemoteResultKind::admission || id != p->core.discovery_id) {
            increment(s.counters.unmatched_response); crypto_wipe(plaintext,sizeof plaintext); return;
        }
    } else if (state != Phase::response_wait || id != p->core.request_id) {
        increment(s.counters.unmatched_response); crypto_wipe(plaintext,sizeof plaintext); return;
    }
    if (d.msg.slot != p->core.acl_slot) { increment(s.counters.auth_failure); crypto_wipe(plaintext,sizeof plaintext); return; }
    if (d.msg.opcode == static_cast<uint8_t>(RemoteRespOpcode::rollover_result) &&
        (opcode(*p) == static_cast<uint8_t>(RemoteCmdOpcode::safe_rollover) || opcode(*p) == static_cast<uint8_t>(RemoteCmdOpcode::force_rollover))) {
        epoch_update(s,*p,d.msg.admin_epoch,d.msg.slot,now);
        complete(s,*p,kRolloverResult,0,d.msg.abandoned_count,now); crypto_wipe(plaintext,sizeof plaintext); return;
    }
    if (d.result_kind == RemoteResultKind::admission) {
        const uint8_t expected_op = state == Phase::rollover_wait ? static_cast<uint8_t>(RemoteCmdOpcode::safe_rollover) : opcode(*p);
        if (d.msg.request_ctl != remote_ctl(expected_op,p->core.acl_slot)) { increment(s.counters.unmatched_response); crypto_wipe(plaintext,sizeof plaintext); return; }
        if (d.msg.admission_code == RemoteAdmission::session_full && expected_op == static_cast<uint8_t>(RemoteCmdOpcode::auth_execute) && !(p->core.flags & kRolled)) {
            p->core.flags |= kRolled; p->core.discovery_id = 0; phase(*p,Phase::rollover_ready);
        } else complete(s,*p,static_cast<uint8_t>(d.result_kind),static_cast<uint8_t>(d.msg.admission_code),d.msg.admission_detail,now);
        crypto_wipe(plaintext,sizeof plaintext); return;
    }
    // Session controls finish only in the rollover/admission domains above. They never
    // own an execution transcript or reserve target ACK capacity.
    if (opcode(*p) != static_cast<uint8_t>(RemoteCmdOpcode::auth_execute) &&
        opcode(*p) != static_cast<uint8_t>(RemoteCmdOpcode::open_execute)) {
        increment(s.counters.unmatched_response); crypto_wipe(plaintext,sizeof plaintext); return;
    }
    auto* a = assembly(s,p->core.request_id);
    if (!a || a->terminal_seen || d.msg.response_seq != a->next_seq) { increment(s.counters.assembly_failure); crypto_wipe(plaintext,sizeof plaintext); return; }
    if (d.msg.opcode == static_cast<uint8_t>(RemoteRespOpcode::output)) {
        if (a->next_seq == UINT8_MAX || !append(s,*a,d.body)) increment(s.counters.assembly_failure);
        else ++a->next_seq;
    } else if (d.result_kind == RemoteResultKind::terminal || d.result_kind == RemoteResultKind::protocol_error) {
        if ((d.terminal == RemoteTerminal::scheduled && d.result_kind == RemoteResultKind::terminal && d.result_detail.size()!=4) || d.result_detail.size()>4) increment(s.counters.assembly_failure);
        else {
            uint32_t detail = 0; for (size_t i=0;i<d.result_detail.size();++i) detail |= uint32_t{d.result_detail[i]} << (8*i);
            complete(s,*p,static_cast<uint8_t>(d.result_kind), d.result_kind==RemoteResultKind::terminal ? static_cast<uint8_t>(d.terminal) : static_cast<uint8_t>(d.protocol_error),detail,now);
        }
    } else increment(s.counters.unmatched_response);
    crypto_wipe(plaintext,sizeof plaintext);
}

void remote_client_service(RemoteClientState& s, uint32_t now, RemoteEntropyFn entropy, void* ctx,
                            IRadminCarrier& carrier, IRemoteLocalDelivery& out) {
    remote_client_expire(s,now);
    for (auto& p : s.pending) if (live(p)) {
        auto state=phase(p);
        if ((state==Phase::compare_ready || state==Phase::rollover_ready) && !p.core.discovery_id) {
            auto error=new_id(s,p.core.discovery_id,entropy,ctx);
            if (error!=RemoteClientError::none) local_complete(s,p,error,now);
        }
        if (phase(p)==Phase::request_ready && !(p.core.flags & kSealed)) {
            uint64_t id=0; const auto error=new_id(s,id,entropy,ctx);
            if (error!=RemoteClientError::none) local_complete(s,p,error,now);
            else {
                const auto old_id=p.core.request_id;
                if (auto* a=assembly(s,old_id)) a->request_id=id;
                if (auto* r=retained(s,old_id)) r->request_id=id;
                p.core.request_id=id;
                p.core.outcome_deadline_ms=now+kOutcomeMs;
                if (seal_request(s,p,{p.sealed,p.core.sealed_len})!=RemoteStatus::ok) local_complete(s,p,RemoteClientError::authentication_failed,now);
            }
        }
        const auto error=send_ready(s,p,carrier);
        if (error==RemoteClientError::carrier_unavailable || error==RemoteClientError::authentication_failed) local_complete(s,p,error,now);
        if (phase(p)!=Phase::complete) continue;
        const auto transport=static_cast<RemoteLocalTransport>(p.core.local_transport);
        if (auto* r=retained(s,p.core.request_id)) {
            if (!out.connected(transport)) { r->reoffer_from_zero=1; continue; }
            if (!r->reoffer_from_zero) continue;
        } else if (auto* a=assembly(s,p.core.request_id)) {
            // The existing delivery byte also marks a reconnect re-offer. Keep bit 1
            // (complete delivery) so an ACK arriving before that re-offer stays valid.
            if (!out.connected(transport)) { a->reserved |= 4; continue; }
            if (a->reserved && !(a->reserved & 4)) continue;
        }
        (void)deliver(s,p,transport,now,out);
    }
    // Bounded round: one attempt per due debt; a full queue preserves every row and exact ACK byte.
    for (auto& d:s.ack_debt) if (d.in_use && static_cast<int32_t>(now-d.next_retry_ms)>=0) {
        if (carrier.tx_queue_full()) break;
        const auto sent=carrier.submit_ack(d.route,{true,d.source_hash},remote_client_carrier(d.route,DATA_TYPE_REMOTE_CMD,d.carrier),d.sealed_ack);
        if (sent!=RemoteClientSend::queued) increment(s.counters.radio_enqueue_failure);
        if (d.retries!=UINT8_MAX) ++d.retries;
        d.next_retry_ms=now+kAckRetryMs; // ownership is not target receipt; retain until authenticated epoch change
    }
}
RemoteClientError remote_client_show(RemoteClientState& s,uint64_t id,RemoteLocalTransport t,uint32_t now,IRemoteLocalDelivery& out) {
    auto* p=pending(s,id); return p ? deliver(s,*p,t,now,out) : RemoteClientError::not_found;
}
RemoteClientError remote_client_ack(RemoteClientState& s,uint64_t id,uint32_t now) {
    auto* p=pending(s,id); if (!p) return RemoteClientError::not_found;
    if (auto* r=retained(s,id)) { if (r->in_use!=2 || !r->delivered) return RemoteClientError::incomplete; }
    else { auto* a=assembly(s,id); if (!a || !a->terminal_seen || (a->reserved & 3)!=2) return RemoteClientError::incomplete; }
    return accept_result(s,*p,now);
}
void remote_client_expire(RemoteClientState& s,uint32_t now) {
    for (auto& p:s.pending) if (live(p) && phase(p)!=Phase::complete && static_cast<int32_t>(now-p.core.outcome_deadline_ms)>=0)
        local_complete(s,p,RemoteClientError::unknown,now);
    for (auto& e:s.sessions) if (e.valid && !session_busy(s,e) && uint32_t(now-e.last_used_ms)>=kOutcomeMs) {
        bool debt=false; for (const auto& d:s.ack_debt) if (d.in_use && d.credential_slot==e.credential_slot && equal_key(d.target_admin_pub,e.target_admin_pub)) debt=true;
        if (!debt) crypto_wipe(&e,sizeof e);
    }
}
uint32_t remote_client_next_expiry(const RemoteClientState& s,uint32_t now) {
    uint32_t next=UINT32_MAX;
    for (const auto& p:s.pending) if (live(p) && phase(p)!=Phase::complete) {
        const int32_t left=static_cast<int32_t>(p.core.outcome_deadline_ms-now); next=std::min(next,left>0?static_cast<uint32_t>(left):0u);
    }
    for (const auto& e:s.sessions) if (e.valid && !session_busy(s,e)) {
        bool debt=false; for (const auto& d:s.ack_debt) if (d.in_use && d.credential_slot==e.credential_slot && equal_key(d.target_admin_pub,e.target_admin_pub)) debt=true;
        if (!debt) { const auto elapsed=uint32_t(now-e.last_used_ms); next=std::min(next,elapsed>=kOutcomeMs?0u:kOutcomeMs-elapsed); }
    }
    return next;
}
bool remote_client_busy(const RemoteClientState& s) {
    for (const auto& p:s.pending) if (live(p)) return true;
    for (const auto& a:s.assemblies) if (a.in_use) return true;
    for (const auto& r:s.retained) if (r.in_use) return true;
    for (const auto& d:s.ack_debt) if (d.in_use) return true;
    return false;
}
bool remote_client_key_in_use(const RemoteClientState& s,uint8_t slot) {
    for (const auto& p:s.pending) if (live(p) && p.core.credential_slot==slot) return true;
    for (const auto& e:s.sessions) if (e.valid && e.credential_slot==slot) return true;
    for (const auto& d:s.ack_debt) if (d.in_use && d.credential_slot==slot) return true;
    return false;
}
bool remote_client_target_in_use(const RemoteClientState& s,uint8_t slot) {
    for (const auto& p:s.pending) if (live(p) && p.core.target_book_slot==slot) return true;
    for (const auto& e:s.sessions) if (e.valid && e.target_book_slot==slot) return true;
    for (const auto& d:s.ack_debt) if (d.in_use && d.target_book_slot==slot) return true;
    return false;
}
uint8_t remote_client_pending_count(const RemoteClientState& s) { uint8_t n=0; for (const auto& p:s.pending) n+=live(p); return n; }
uint8_t remote_client_retained_count(const RemoteClientState& s) {
    uint8_t n=0; for (const auto& r:s.retained) n+=r.in_use==2;
    for (const auto& a:s.assemblies) n+=a.in_use && a.terminal_seen;
    return n;
}
const char* remote_client_error_name(RemoteClientError e) {
    switch (e) {
        case RemoteClientError::none: return "ok";
        case RemoteClientError::bad_args: return "bad_args";
        case RemoteClientError::authentication_failed: return "authentication_failed";
        case RemoteClientError::entropy_failed: return "entropy_failed";
        case RemoteClientError::duplicate_id: return "duplicate_id";
        case RemoteClientError::request_table_full: return "request_table_full";
        case RemoteClientError::assembly_full: return "assembly_full";
        case RemoteClientError::result_full: return "result_full";
        case RemoteClientError::ack_debt_full: return "ack_debt_full";
        case RemoteClientError::correlation_full: return "correlation_full";
        case RemoteClientError::session_cache_full: return "session_cache_full";
        case RemoteClientError::carrier_unavailable: return "carrier_unavailable";
        case RemoteClientError::radio_enqueue_failed: return "radio_enqueue_failed";
        case RemoteClientError::not_found: return "not_found";
        case RemoteClientError::incomplete: return "incomplete";
        case RemoteClientError::unknown: return "unknown";
    }
    return "internal_error";
}
} // namespace MESHROUTE_NS
