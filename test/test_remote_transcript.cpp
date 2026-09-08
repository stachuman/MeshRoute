// MeshRoute — 7b-1 real admission, transcript storage, sealing and authenticated ACK lifecycle.
#include "doctest.h"
#include "remote_session.h"
#include "frame_codec.h"
#include "identity.h"
#include "monocypher.h"
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace meshroute;
struct Pool {
    RemoteSessionState state{};
    Identity root{}, controllers[2]{};
    Pool() {
        uint8_t seed[32] = {1}; identity_from_seed(root, seed);
        RemoteSessionInstall p{}; p.set_root = p.set_acl = true;
        memcpy(p.x_secret, root.x_secret, 32); memcpy(p.ed_pub, root.ed_pub, 32);
        for (uint8_t slot = 0; slot < 2; ++slot) {
            seed[0] = slot + 2; identity_from_seed(controllers[slot], seed);
            memcpy(p.acl[slot].ed_pub, controllers[slot].ed_pub, 32);
            p.acl[slot].role = slot ? kRadminRoleOperator : kRadminRoleOwner;
            p.epoch_set[slot] = true; p.epoch[slot] = 0x1234 + slot;
        }
        remote_session_install(state, p);
    }
    void key(uint8_t slot, uint8_t (&session)[32]) {
        uint8_t x[32], shared[32], base[32];
        ed_pub_to_x25519(x, root.ed_pub);
        CHECK(remote_ecdh_shared(shared, controllers[slot], x) == RemoteStatus::ok);
        CHECK(remote_kdf_base(base, shared, controllers[slot].ed_pub, root.ed_pub) == RemoteStatus::ok);
        CHECK(remote_kdf_session(session, base, state.epoch[slot]) == RemoteStatus::ok);
        crypto_wipe(x, sizeof x); crypto_wipe(shared, sizeof shared); crypto_wipe(base, sizeof base);
    }
    ReplyRoute route(uint8_t depth = 0) {
        ReplyRoute r{};
        r.carrier = static_cast<uint8_t>(depth ? RadminCarrierKind::cross_layer : RadminCarrierKind::same_layer);
        if (depth) { r.n_layers = depth; r.cur = depth - 1; for (uint8_t i = 0; i < depth; ++i) r.layer_ids[i] = i + 1; }
        return r;
    }
    RemoteRxResult receive(uint64_t id, uint8_t slot = 0, bool ack = false, uint8_t depth = 0,
                           uint64_t now = 100, bool corrupt = false, const char* line = "status") {
        RemoteRxInput in{}; in.now_ms = now; in.outer_type = DATA_TYPE_REMOTE_CMD;
        in.route = route(depth); in.reply_carrier = remote_reply_carrier(in.route);
        in.request_carrier = in.reply_carrier; in.request_carrier.outer_data_type = DATA_TYPE_REMOTE_CMD;
        if (depth) in.request_carrier.path_cursor = depth - 1;
        in.source = {true, controllers[slot].key_hash32};
        RemoteMessage m{}; m.outer_type = DATA_TYPE_REMOTE_CMD; m.slot = slot; m.request_id = id;
        m.opcode = static_cast<uint8_t>(ack ? RemoteCmdOpcode::response_ack : RemoteCmdOpcode::auth_execute);
        uint8_t session[32]; key(slot, session);
        const RemoteKeys keys{{}, session};
        std::array<uint8_t, kRadminBodyBytes> wire{}; size_t n = 0;
        const std::span<const uint8_t> body = ack ? std::span<const uint8_t>{}
            : std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(line), strlen(line));
        CHECK(remote_body_encode(wire, n, m, body, keys, in.source, in.request_carrier) == RemoteStatus::ok);
        crypto_wipe(session, sizeof session);
        if (corrupt && n) wire[n - 1] ^= 1;
        in.body = {wire.data(), n};
        RemoteRxResult r{}; remote_session_receive(state, in, r); return r;
    }
    uint8_t admit(uint64_t id, uint8_t slot = 0, uint8_t depth = 0) {
        const auto r = receive(id, slot, false, depth);
        CHECK(r.verdict == RemoteAdmitVerdict::admit);
        return r.seen_index;
    }
    bool reserve(uint8_t si) {
        if (si >= kRadminSeenSlots) { CHECK(si < kRadminSeenSlots); return false; }
        size_t cap = 0;
        CHECK(remote_body_cap(remote_reply_carrier(state.seen[si].route), cap) == RemoteStatus::ok);
        return remote_transcript_reserve(state, si, static_cast<uint16_t>(cap - kRemoteOverheadAuthResponse));
    }
    void finish(uint8_t si, const std::string& text = "", RemoteTerminal result = RemoteTerminal::completed) {
        const bool ok = reserve(si); CHECK(ok); if (!ok) return;
        // Intentionally uneven write spans, including no-op empty appends.
        for (size_t pos = 0; pos < text.size();) {
            const size_t n = std::min(size_t{17}, text.size() - pos);
            remote_transcript_append(state, si, reinterpret_cast<const uint8_t*>(text.data() + pos), n); pos += n;
        }
        remote_transcript_append(state, si, nullptr, 0);
        remote_transcript_complete(state, si, result);
    }
    std::vector<std::vector<uint8_t>> drain(std::string& text, RemoteTerminal& result, uint8_t& terminal_seq) {
        std::vector<std::vector<uint8_t>> frames;
        for (unsigned bound = 0; bound < 36; ++bound) {
            const uint8_t si = remote_transcript_next(state); if (si == kRadminNoSlot) break;
            const auto& e = state.seen[si];
            std::array<uint8_t, kRadminBodyBytes> wire{}, plain{}; size_t n = 0;
            const auto status = remote_transcript_encode(state, si, wire, n);
            CHECK(status == RemoteStatus::ok); if (status != RemoteStatus::ok) break;
            uint8_t session[32]; key(e.record.controller_slot, session);
            const RemoteKeys keys{{}, session}; RemoteDecoded d{};
            CHECK(remote_body_decode(d, DATA_TYPE_REMOTE_RESP, {wire.data(), n}, keys,
                  {true, e.record.source_hash}, remote_reply_carrier(e.route), plain) == RemoteStatus::ok);
            CHECK(d.msg.request_id == e.record.request_id); CHECK(d.msg.slot == e.record.controller_slot);
            CHECK(d.msg.response_seq == state.transcripts[e.record.transcript_slot].next_seq_to_send);
            if (d.msg.opcode == static_cast<uint8_t>(RemoteRespOpcode::output)) {
                text.append(reinterpret_cast<const char*>(d.body.data()), d.body.size());
                CHECK(n == d.body.size() + kRemoteOverheadAuthResponse);
            } else {
                CHECK(d.msg.opcode == static_cast<uint8_t>(RemoteRespOpcode::terminal));
                result = d.terminal; terminal_seq = d.msg.response_seq;
                CHECK(d.result_detail.empty()); CHECK(n == kRemoteOverheadAuthResponse + 1);
            }
            frames.emplace_back(wire.begin(), wire.begin() + n);
            remote_transcript_sent(state, si);
            crypto_wipe(session, sizeof session); crypto_wipe(plain.data(), plain.size());
        }
        CHECK(remote_transcript_next(state) == kRadminNoSlot);
        return frames;
    }
};
}

TEST_CASE("§radmin-7/transcript live cap binds storage and each retained cross-layer chunk size") {
    for (uint8_t depth : {0, 2, 3, 4}) {
        Pool p; const uint8_t si = p.admit(1, 0, depth);
        size_t cap = 0; CHECK(remote_body_cap(remote_reply_carrier(p.route(depth)), cap) == RemoteStatus::ok);
        const size_t chunk = cap - kRemoteOverheadAuthResponse;
        if (depth == 0) CHECK(chunk == kRadminChunkBytes);
        CHECK_FALSE(remote_transcript_reserve(p.state, si, static_cast<uint16_t>(chunk + 1)));
        const std::string expected(chunk * 2 + 3, 'X'); p.finish(si, expected);
        std::string text; RemoteTerminal term{}; uint8_t seq = 255;
        const auto frames = p.drain(text, term, seq);
        CHECK(text == expected); CHECK(term == RemoteTerminal::completed); CHECK(seq == 3); CHECK(frames.size() == 4);
        CHECK(frames[0].size() == cap); CHECK(frames[1].size() == cap);
    }
}
TEST_CASE("§radmin-7/transcript silent command retains one chunk but sends only terminal zero") {
    Pool p; const uint8_t si = p.admit(0); p.finish(si);
    std::string text; RemoteTerminal term{}; uint8_t seq = 255;
    const auto frames = p.drain(text, term, seq);
    CHECK(frames.size() == 1); CHECK(text.empty()); CHECK(seq == 0); CHECK(term == RemoteTerminal::completed);
    CHECK(p.state.seen[si].record.state == static_cast<uint8_t>(SeenState::completed));
}
TEST_CASE("§radmin-7/transcript eight chunks retain exact prefix and truthful truncation") {
    for (const size_t extra : {size_t{0}, size_t{1}, size_t{200}}) {
        Pool p; const uint8_t si = p.admit(1);
        const std::string source(kRadminChunkBytes * kRadminChunkSlots + extra, 'Z'); p.finish(si, source);
        std::string text; RemoteTerminal term{}; uint8_t seq = 0;
        const auto frames = p.drain(text, term, seq);
        CHECK(text == source.substr(0, 1648)); CHECK(frames.size() == 9); CHECK(seq == 8);
        CHECK(term == (extra ? RemoteTerminal::output_truncated : RemoteTerminal::completed));
        CHECK(p.state.transcripts[p.state.seen[si].record.transcript_slot].bytes_total == 1648);
    }
}
TEST_CASE("§radmin-7/transcript completed retry starts at zero byte-identically without re-reserving") {
    Pool p; const uint8_t si = p.admit(1); p.finish(si, std::string(300, 'R'));
    std::string text; RemoteTerminal term{}; uint8_t seq = 0;
    const auto original = p.drain(text, term, seq);
    CHECK(p.receive(1).verdict == RemoteAdmitVerdict::replay_transcript);
    CHECK_FALSE(p.reserve(si)); CHECK(remote_session_ingress_used(p.state) == 0);
    std::string replay; CHECK(p.drain(replay, term, seq) == original); CHECK(replay == text);
    CHECK(p.receive(1, 0, false, 0, 100, false, "version").verdict == RemoteAdmitVerdict::reject_id_reuse);
}
TEST_CASE("§radmin-7/transcript retry while capturing never rewinds or dispatches twice") {
    Pool p; const uint8_t si = p.admit(1); CHECK(p.reserve(si));
    const uint8_t text[] = {'o', 'k'}; remote_transcript_append(p.state, si, text, 2);
    CHECK(p.receive(1).verdict == RemoteAdmitVerdict::replay_transcript);
    CHECK_FALSE(p.reserve(si)); CHECK(remote_transcript_next(p.state) == kRadminNoSlot);
    remote_transcript_complete(p.state, si, RemoteTerminal::completed);
    std::string out; RemoteTerminal term{}; uint8_t seq = 0;
    CHECK(p.drain(out, term, seq).size() == 2); CHECK(out == "ok");
}
TEST_CASE("§radmin-7/transcript ACK outcomes authenticate and consume no ingress") {
    Pool p; const uint8_t si = p.admit(1);
    CHECK(p.receive(1, 0, true).verdict == RemoteAdmitVerdict::ack_premature);
    CHECK(remote_session_ingress_used(p.state) == 1); CHECK(p.reserve(si));
    CHECK(p.receive(1, 0, true).verdict == RemoteAdmitVerdict::ack_premature);
    remote_transcript_complete(p.state, si, RemoteTerminal::completed);
    const auto ti = p.state.seen[si].record.transcript_slot;
    CHECK(p.receive(1, 0, true, 0, 100, true).verdict == RemoteAdmitVerdict::silent_auth_failed);
    CHECK(p.state.seen[si].record.transcript_slot == ti);
    CHECK(p.receive(1, 0, true).verdict == RemoteAdmitVerdict::ack_released);
    CHECK(p.state.seen[si].record.transcript_slot == kRadminNoTranscript);
    CHECK(p.state.seen[si].record.state == static_cast<uint8_t>(SeenState::acknowledged));
    CHECK(p.receive(1, 0, true).verdict == RemoteAdmitVerdict::ack_duplicate);
    CHECK(p.receive(2, 0, true).verdict == RemoteAdmitVerdict::ack_unknown);
    CHECK(remote_session_ingress_used(p.state) == 0); CHECK(remote_session_seen_used(p.state) == 1);
    CHECK(p.receive(1).verdict == RemoteAdmitVerdict::already_acknowledged);
    for (const auto& c : p.state.chunks) { CHECK(c.len == kRadminNoChunk); for (uint8_t b : c.bytes) CHECK(b == 0); }
}
TEST_CASE("§radmin-7/transcript pool pressure ACK frees header for owner AND operator without touching waiter") {
    for (const uint8_t waiter : {0, 1}) {
        Pool p;
        for (uint8_t i = 0; i < 4; ++i) p.finish(p.admit(i + 1), "held");
        const uint8_t si = p.admit(9, waiter);
        CHECK_FALSE(p.reserve(si)); CHECK(p.state.transcript_exhaustion == 1);
        CHECK(p.receive(9, waiter).verdict == RemoteAdmitVerdict::replay_transcript);
        RadminIngressView v{}; CHECK(remote_next_admitted(p.state, v)); CHECK(v.seen_index == si);
        const auto h = p.state.ingress[waiter ? kRadminIngressGeneral : kRadminIngressControl];
        const auto body = p.state.body[h.body_slot];
        const auto r = p.receive(1, 0, true); CHECK(r.verdict == RemoteAdmitVerdict::ack_released);
        CHECK(r.ingress_index == kRadminNoSlot);
        CHECK(memcmp(&body, &p.state.body[h.body_slot], sizeof body) == 0);
        CHECK(remote_session_ingress_used(p.state) == 1);
        CHECK(p.reserve(si));
        remote_transcript_complete(p.state, si, RemoteTerminal::completed);
        CHECK(remote_session_ingress_used(p.state) == 0);
        CHECK(remote_session_seen_used(p.state) == 5);
    }
}
TEST_CASE("§radmin-7/transcript free header without free chunk still refuses atomically") {
    Pool p; p.finish(p.admit(1), std::string(1648, 'A'));
    const uint8_t si = p.admit(2);
    CHECK_FALSE(p.reserve(si)); CHECK(p.state.seen[si].record.transcript_slot == kRadminNoTranscript);
    CHECK(p.state.seen[si].record.state == static_cast<uint8_t>(SeenState::admitted));
    CHECK(p.state.transcript_exhaustion == 1);
    CHECK(p.receive(1, 0, true).verdict == RemoteAdmitVerdict::ack_released);
    CHECK(p.reserve(si));
}
TEST_CASE("§radmin-7/transcript expiry drops only never-executed work, no transcript TTL") {
    Pool p; const uint8_t kept = p.admit(1); p.finish(kept, "held"); const uint8_t waiting = p.admit(2);
    CHECK(remote_session_expire(p.state, 100 + radmin_staging_lifetime_ms) == 1);
    CHECK(p.state.seen[kept].record.state == static_cast<uint8_t>(SeenState::completed));
    CHECK(p.state.seen[waiting].record.state == static_cast<uint8_t>(SeenState::free));
    CHECK(p.receive(2, 0, false, 0, 100 + radmin_staging_lifetime_ms + 1).verdict == RemoteAdmitVerdict::admit);
    CHECK(remote_transcript_next(p.state) == kept);
}
TEST_CASE("§radmin-7/transcript epoch invalidation abandons pending work and wipes only affected transcripts") {
    Pool p; const uint8_t a = p.admit(1); p.finish(a, "secret"); const uint8_t b = p.admit(2, 1); p.finish(b, "keep");
    const uint8_t pending = p.admit(3);
    const uint16_t ci = p.state.transcripts[p.state.seen[a].record.transcript_slot].first_chunk;
    RemoteSessionInstall change{}; change.set_acl = true;
    memcpy(change.acl, p.state.acl, sizeof change.acl);
    change.acl[0].role = kRadminRoleOperator; change.epoch_set[0] = true; change.epoch[0] = 999;
    remote_session_install(p.state, change);
    CHECK(p.state.seen[a].record.state == static_cast<uint8_t>(SeenState::free));
    CHECK(p.state.seen[pending].record.state == static_cast<uint8_t>(SeenState::free));
    CHECK(remote_session_ingress_used(p.state) == 0);
    CHECK(remote_transcript_next(p.state) == b);
    CHECK(p.state.chunks[ci].len == kRadminNoChunk); for (uint8_t v : p.state.chunks[ci].bytes) CHECK(v == 0);
    remote_session_clear(p.state);
    CHECK(remote_transcript_next(p.state) == kRadminNoSlot);
    for (const auto& c : p.state.chunks) { CHECK(c.len == kRadminNoChunk); for (uint8_t v : c.bytes) CHECK(v == 0); }
}
TEST_CASE("§radmin-7/transcript encode refusal cannot move cursor or mutate retained plaintext") {
    Pool p; const uint8_t si = p.admit(1); p.finish(si, "held");
    std::array<uint8_t, 1> tiny{}; size_t n = 0;
    CHECK(remote_transcript_encode(p.state, si, tiny, n) == RemoteStatus::bad_buffer);
    CHECK(n == 0); CHECK(p.state.transcripts[p.state.seen[si].record.transcript_slot].next_seq_to_send == 0);
    std::string out; RemoteTerminal term{}; uint8_t seq = 0; p.drain(out, term, seq); CHECK(out == "held");
}
TEST_CASE("§radmin-7/transcript failed reseal preserves terminal; synthetic replacement would reuse its nonce") {
    Pool p; const uint8_t si = p.admit(1); p.finish(si);
    std::array<uint8_t, kRadminBodyBytes> original{}, replay{}, replacement{};
    size_t n = 0, rn = 0, changed_n = 0;
    CHECK(remote_transcript_encode(p.state, si, original, n) == RemoteStatus::ok);
    remote_transcript_sent(p.state, si);
    CHECK(p.receive(1).verdict == RemoteAdmitVerdict::replay_transcript);
    std::array<uint8_t, 1> too_small{};
    CHECK(remote_transcript_encode(p.state, si, too_small, rn) == RemoteStatus::bad_buffer);
    CHECK(rn == 0);
    CHECK(remote_transcript_encode(p.state, si, replay, rn) == RemoteStatus::ok);
    CHECK(n == rn); CHECK(memcmp(original.data(), replay.data(), n) == 0);

    // B374 preflight evidence, SYNTHETIC proposed fallback only: never mutate the
    // production state. Changing a previously sealed terminal byte is not a new nonce.
    RemoteSessionState copy = p.state;
    copy.transcripts[copy.seen[si].record.transcript_slot].terminal = static_cast<uint8_t>(RemoteTerminal::internal_error);
    CHECK(remote_transcript_encode(copy, si, replacement, changed_n) == RemoteStatus::ok);
    CHECK(n == changed_n); CHECK(memcmp(original.data(), replacement.data(), n) != 0);
    uint8_t key[32]; p.key(0, key);
    RemoteDecoded a{}, b{}; std::array<uint8_t, kRadminBodyBytes> ap{}, bp{};
    const auto source = RemoteSource{true, p.controllers[0].key_hash32};
    const auto carrier = remote_reply_carrier(p.route());
    CHECK(remote_body_decode(a, DATA_TYPE_REMOTE_RESP, {original.data(), n}, RemoteKeys{{}, key}, source, carrier, ap) == RemoteStatus::ok);
    CHECK(remote_body_decode(b, DATA_TYPE_REMOTE_RESP, {replacement.data(), changed_n}, RemoteKeys{{}, key}, source, carrier, bp) == RemoteStatus::ok);
    CHECK(a.terminal == RemoteTerminal::completed); CHECK(b.terminal == RemoteTerminal::internal_error);
    RemoteLayout layout{};
    CHECK(remote_layout(DATA_TYPE_REMOTE_RESP, remote_ctl(a.msg.opcode, a.msg.slot), layout) == RemoteStatus::ok);
    uint8_t na[24], nb[24];
    CHECK(remote_nonce(na, layout, a.msg, key, source) == RemoteStatus::ok);
    CHECK(remote_nonce(nb, layout, b.msg, key, source) == RemoteStatus::ok);
    CHECK(memcmp(na, nb, sizeof na) == 0);
    crypto_wipe(key, sizeof key); crypto_wipe(&copy, sizeof copy);
}
