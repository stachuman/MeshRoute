// MeshRoute — 7b-1 real admission, transcript storage, sealing and authenticated ACK lifecycle.
#include "doctest.h"
#include "remote_session.h"
#include "frame_codec.h"
#include "identity.h"
#include "monocypher.h"
#include "remote_activation.h"
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
                           uint64_t now = 100, bool corrupt = false, const char* line = "status", uint32_t source = UINT32_MAX) {
        RemoteRxInput in{}; in.now_ms = now; in.outer_type = DATA_TYPE_REMOTE_CMD;
        in.route = route(depth); in.reply_carrier = remote_reply_carrier(in.route);
        in.request_carrier = in.reply_carrier; in.request_carrier.outer_data_type = DATA_TYPE_REMOTE_CMD;
        if (depth) in.request_carrier.path_cursor = depth - 1;
        in.source = {true, source == UINT32_MAX ? controllers[slot].key_hash32 : source};
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
    std::array<uint8_t, sizeof(RemoteSessionState)> frozen{};
    memcpy(frozen.data(), &p.state, frozen.size());
    remote_transcript_complete(p.state, si, RemoteTerminal::internal_error);
    const uint8_t late[] = {'l', 'a', 't', 'e'};
    remote_transcript_append(p.state, si, late, sizeof late);
    CHECK(memcmp(frozen.data(), &p.state, frozen.size()) == 0); // immutable BEFORE first publication too
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

TEST_CASE("§radmin-73/layout complete ownership costs exactly the approved 80 bytes") {
    CHECK(sizeof(DeferredActionRecord) == 40); CHECK(alignof(DeferredActionRecord) == 8);
    CHECK(sizeof(TranscriptHeader) == 32); CHECK(sizeof(RemoteSessionState) == 8904);
    CHECK(sizeof(RemoteSessionState) - 8824 == 4 * (32 - 24) + 40 + 2 + 6);
    CHECK(offsetof(RemoteSessionState, action) == 8856);
    CHECK(offsetof(RemoteSessionState, last_activation_kind) == 8896);
    Pool p; DeferredActionRecord transfer{};
    memset(&transfer, 0xFF, sizeof transfer);
    CHECK_FALSE(remote_action_take(p.state, UINT64_MAX, transfer));
    const DeferredActionRecord empty{}; CHECK(memcmp(&transfer, &empty, sizeof empty) == 0);
}

TEST_CASE("§radmin-73/terminal actual encoder freezes five LE bytes independently on every carrier") {
    for (const uint8_t depth : {0, 2, 3, 4}) for (const uint32_t delay : {7006u, 299999u}) {
        Pool p; const auto si = p.admit(0, 0, depth); CHECK(p.reserve(si));
        CHECK(remote_action_reserve(p.state, 0, 0, 4, 1, delay) == RemoteTerminal::scheduled);
        CHECK(p.state.action.phase == RemoteActionPhase::preparing);
        const auto original = p.state.action;
        CHECK(original.request_id == 0); CHECK(original.admin_epoch == p.state.epoch[0]);
        CHECK(original.source_hash == p.controllers[0].key_hash32); CHECK(original.controller_slot == 0);
        CHECK(original.authority == kRadminRoleOwner); CHECK(original.kind == 4); CHECK(original.backend == 1);
        CHECK(original.activate_at_ms == 0); CHECK(original.trigger == RemoteActionTrigger::none);
        CHECK(original.reserved[0] == 0); CHECK(original.reserved[1] == 0);
        remote_transcript_complete(p.state, si, RemoteTerminal::scheduled);
        CHECK(remote_session_ingress_used(p.state) == 0);
        CHECK(p.state.action.phase == RemoteActionPhase::prepared);
        const auto ti = p.state.seen[si].record.transcript_slot;
        CHECK(p.state.transcripts[ti].activation_ms == delay);
        std::array<uint8_t, kRadminBodyBytes> wire{}, plain{}, again{}; size_t n = 0, n2 = 0;
        CHECK(remote_transcript_encode(p.state, si, wire, n) == RemoteStatus::ok);
        CHECK(n == kRemoteOverheadAuthResponse + 5);
        uint8_t session[32]; p.key(0, session); RemoteDecoded d{};
        CHECK(remote_body_decode(d, DATA_TYPE_REMOTE_RESP, {wire.data(), n}, RemoteKeys{{}, session},
              {true, p.controllers[0].key_hash32}, remote_reply_carrier(p.route(depth)), plain) == RemoteStatus::ok);
        CHECK(d.terminal == RemoteTerminal::scheduled); CHECK(d.result_detail.size() == 4);
        for (uint8_t i = 0; i < 4; ++i) CHECK(d.result_detail[i] == uint8_t(delay >> (8 * i)));
        CHECK(d.msg.response_seq == 0);
        remote_action_owned(p.state, si, 1000); remote_transcript_sent(p.state, si);
        DeferredActionRecord owned{}; CHECK(remote_action_take(p.state, 1000 + delay, owned));
        CHECK(owned.kind == original.kind); CHECK(owned.backend == original.backend);
        CHECK(owned.activate_at_ms == 1000 + delay); CHECK(owned.trigger == RemoteActionTrigger::deadline);
        CHECK(p.state.action.phase == RemoteActionPhase::none);
        CHECK(p.receive(0, 0, false, depth).verdict == RemoteAdmitVerdict::replay_transcript);
        CHECK(remote_transcript_encode(p.state, si, again, n2) == RemoteStatus::ok);
        CHECK(n == n2); CHECK(memcmp(wire.data(), again.data(), n) == 0);
        CHECK_FALSE(remote_action_take(p.state, UINT64_MAX, owned));
        CHECK(n + 1 + 2 * 4 + 2 + protocol::gw_env_max_hops == remote_scheduled_terminal_inner_len);
        // Price the ACTUAL session-produced body through the real inner/DATA packers, including typed wrappers.
        for(bool wrapper:{false,true}) {
            if(wrapper&&depth==4)continue; // a home gateway must still prepend its own hop
            uint8_t flags=DATA_FLAG_SOURCE_HASH|DATA_FLAG_DST_HASH;
            if(depth)flags|=DATA_FLAG_CROSS_LAYER;
            if(wrapper&&!depth)flags|=DATA_FLAG_MS_ENCLOSED_TYPE;
            std::vector<uint8_t> body;
            if(wrapper)body.push_back(DATA_TYPE_REMOTE_RESP);
            body.insert(body.end(),wire.begin(),wire.begin()+n);
            uint8_t inner[protocol::max_payload_bytes_hard_cap]={};const auto route=p.route(depth);
            const auto inner_n=pack_unicast_inner(inner,flags,p.controllers[0].key_hash32,route.layer_ids,
                depth,depth?1:0,7,p.root.key_hash32,body.data(),body.size(),0,0);
            CHECK(inner_n>0);CHECK(inner_n<=remote_scheduled_terminal_inner_len);
            CHECK(inner_n==n+9+(depth?depth+2:0)+(wrapper?1:0));
            if(depth==(wrapper?3:4))CHECK(inner_n==46);
            const auto parsed=parse_unicast_inner({inner,inner_n},flags);CHECK(parsed.has_value());
            if(parsed){CHECK(parsed->body.size()==body.size());CHECK(memcmp(parsed->body.data(),body.data(),body.size())==0);}
            data_in frame{};frame.type=wrapper?DATA_TYPE_MOBILE_SEND:DATA_TYPE_REMOTE_RESP;frame.flags=flags;
            frame.next=3;frame.dst=9;frame.ctr=123;frame.inner={inner,inner_n};uint8_t mac[4]={};frame.mac=mac;
            uint8_t packed[protocol::lora_max_frame_bytes]={};const auto packet_n=pack_data(frame,packed);
            CHECK(packet_n>0);CHECK(packet_n<=protocol::lora_max_frame_bytes);CHECK(parse_data({packed,packet_n}).has_value());
        }

        crypto_wipe(session, sizeof session);
    }
}

TEST_CASE("§radmin-73/ownership output and early ACK cannot arm or destroy the unowned promise") {
    Pool p; const auto si = p.admit(12); CHECK(p.reserve(si));
    const char output[] = "retained output";
    remote_transcript_append(p.state, si, reinterpret_cast<const uint8_t*>(output), sizeof output - 1);
    CHECK(remote_action_reserve(p.state, 0, 12, 3, 4, 10000) == RemoteTerminal::scheduled);
    CHECK(p.receive(12, 0, true).verdict == RemoteAdmitVerdict::ack_premature);
    remote_transcript_complete(p.state, si, RemoteTerminal::scheduled);
    const auto frozen = p.state;
    CHECK(p.receive(12, 0, true).verdict == RemoteAdmitVerdict::ack_premature);
    CHECK(memcmp(&p.state, &frozen, sizeof frozen) == 0);
    remote_action_owned(p.state, si, 100); // OUTPUT ownership is insufficient
    CHECK(p.state.action.phase == RemoteActionPhase::prepared); CHECK(p.state.action.activate_at_ms == 0);
    remote_transcript_sent(p.state, si);
    CHECK(p.receive(12, 0, true).verdict == RemoteAdmitVerdict::ack_premature);
    DeferredActionRecord transfer{}; CHECK_FALSE(remote_action_take(p.state, UINT64_MAX, transfer));
    remote_action_owned(p.state, si, 200);
    CHECK(p.state.action.phase == RemoteActionPhase::armed); CHECK(p.state.action.activate_at_ms == 10200);
    CHECK(remote_session_earliest_deadline(p.state) == 10200);
    remote_transcript_sent(p.state, si);
    CHECK(p.receive(12).verdict == RemoteAdmitVerdict::replay_transcript);
    remote_action_owned(p.state, si, 9000); remote_transcript_sent(p.state, si);
    remote_action_owned(p.state, si, 9001); remote_transcript_sent(p.state, si);
    CHECK(p.state.action.activate_at_ms == 10200); // replay never refreshes the clock
    CHECK(p.receive(12, 0, true, 0, 300, true).verdict == RemoteAdmitVerdict::silent_auth_failed);
    CHECK(p.receive(12, 1, true).verdict == RemoteAdmitVerdict::ack_unknown);
    CHECK(p.receive(12, 0, true, 0, 300, false, "status", 123).verdict == RemoteAdmitVerdict::ack_unknown);
    CHECK(p.state.action.phase == RemoteActionPhase::armed);
    CHECK(p.receive(12, 0, true, 0, 300).verdict == RemoteAdmitVerdict::ack_released);
    CHECK(p.state.action.phase == RemoteActionPhase::due); CHECK(p.state.action.trigger == RemoteActionTrigger::ack);
    CHECK(p.state.seen[si].record.transcript_slot == kRadminNoTranscript);
    CHECK(remote_session_earliest_deadline(p.state) == UINT64_MAX);
    CHECK(p.receive(12, 0, true).verdict == RemoteAdmitVerdict::ack_duplicate);
    CHECK(remote_action_take(p.state, 300, transfer)); CHECK(transfer.activation_ms == 10000);
    CHECK(transfer.trigger == RemoteActionTrigger::ack); CHECK(transfer.kind == 3); CHECK(transfer.backend == 4);
    CHECK_FALSE(remote_action_take(p.state, 500, transfer));
}

TEST_CASE("§radmin-73/deadline exact edge saturation and late loops consume once without a zero-delay scan") {
    for (const uint64_t now : {uint64_t{0}, uint64_t{UINT32_MAX - 2}, UINT64_MAX - 5, UINT64_MAX}) {
        Pool p; const auto si = p.admit(5); CHECK(p.reserve(si));
        CHECK(remote_action_reserve(p.state, 0, 5, 2, 0, 10) == RemoteTerminal::scheduled);
        remote_transcript_complete(p.state, si, RemoteTerminal::scheduled);
        remote_action_owned(p.state, si, now);
        const uint64_t deadline = remote_session_deadline(now, 10);
        CHECK(p.state.action.activate_at_ms == deadline);
        DeferredActionRecord transfer{};
        CHECK_FALSE(remote_action_take(p.state, deadline - 1, transfer));
        CHECK(p.state.action.phase == RemoteActionPhase::armed);
        (void)remote_session_expire(p.state, deadline);
        CHECK(p.state.action.phase == RemoteActionPhase::due);
        CHECK(remote_session_earliest_deadline(p.state) == UINT64_MAX);
        CHECK(remote_action_take(p.state, UINT64_MAX, transfer));
        CHECK(transfer.trigger == RemoteActionTrigger::deadline); CHECK(transfer.activate_at_ms == deadline);
        CHECK_FALSE(remote_action_take(p.state, UINT64_MAX, transfer));
    }
}

TEST_CASE("§radmin-73/conflict is immutable across credentials while no-capacity never prepares") {
    Pool p;
    CHECK(remote_action_reserve(p.state, 0, 1, 1, 1, 100) == RemoteTerminal::internal_error);
    const auto first = p.admit(1); CHECK(p.reserve(first));
    CHECK(remote_action_reserve(p.state, 0, 1, 1, 1, 100) == RemoteTerminal::scheduled);
    remote_transcript_complete(p.state, first, RemoteTerminal::scheduled);
    const auto second = p.admit(2, 1); CHECK(p.reserve(second));
    const auto action = p.state.action;
    CHECK(remote_action_reserve(p.state, 1, 2, 2, 0, 200) == RemoteTerminal::action_busy);
    CHECK(memcmp(&p.state.action, &action, sizeof action) == 0);
    remote_transcript_complete(p.state, second, RemoteTerminal::action_busy);
    std::array<uint8_t, kRadminBodyBytes> before{}, after{}; size_t a = 0, b = 0;
    CHECK(remote_transcript_encode(p.state, second, before, a) == RemoteStatus::ok);
    remote_action_owned(p.state, first, 1000); DeferredActionRecord transfer{};
    CHECK(remote_action_take(p.state, 1100, transfer));
    CHECK(p.receive(2, 1).verdict == RemoteAdmitVerdict::replay_transcript);
    CHECK(remote_transcript_encode(p.state, second, after, b) == RemoteStatus::ok);
    CHECK(a == b); CHECK(memcmp(before.data(), after.data(), a) == 0);
    CHECK(p.state.transcripts[p.state.seen[second].record.transcript_slot].terminal == uint8_t(RemoteTerminal::action_busy));
    CHECK(p.state.transcript_exhaustion == 0); CHECK(p.state.response_enqueue_failure == 0);
    CHECK(p.state.response_seal_failure == 0);
}

TEST_CASE("§radmin-73/completion truncation preserves scheduled while staging failure aborts the plan") {
    for (const bool failure : {false, true}) {
        Pool p; const auto si = p.admit(4); CHECK(p.reserve(si));
        CHECK(remote_action_reserve(p.state, 0, 4, 4, 1, 12345) == RemoteTerminal::scheduled);
        const std::string big(2000, '!'); remote_transcript_append(p.state, si,
            reinterpret_cast<const uint8_t*>(big.data()), big.size());
        remote_transcript_complete(p.state, si, failure ? RemoteTerminal::internal_error : RemoteTerminal::scheduled);
        const auto& h = p.state.transcripts[p.state.seen[si].record.transcript_slot];
        CHECK(h.truncated == 1); CHECK(h.frames == 9);
        CHECK(h.terminal == uint8_t(failure ? RemoteTerminal::internal_error : RemoteTerminal::scheduled));
        CHECK(h.activation_ms == (failure ? 0 : 12345));
        CHECK(p.state.action.phase == (failure ? RemoteActionPhase::none : RemoteActionPhase::prepared));
        const auto frozen = p.state; remote_transcript_complete(p.state, si, RemoteTerminal::refused);
        CHECK(memcmp(&p.state, &frozen, sizeof frozen) == 0);
    }
    Pool p; const auto si = p.admit(6); CHECK(p.reserve(si));
    remote_transcript_complete(p.state, si, RemoteTerminal::scheduled);
    CHECK(p.state.transcripts[p.state.seen[si].record.transcript_slot].terminal == uint8_t(RemoteTerminal::internal_error));
}

TEST_CASE("§radmin-73/invalidation releases unarmed work only; status and last results remain truthful") {
    for (const bool armed : {false, true}) for (const bool all : {false, true}) {
        Pool p; const auto si = p.admit(7); CHECK(p.reserve(si));
        CHECK(remote_action_reserve(p.state, 0, 7, 5, 0, 300) == RemoteTerminal::scheduled);
        remote_transcript_complete(p.state, si, RemoteTerminal::scheduled);
        if (armed) remote_action_owned(p.state, si, 100);
        p.state.last_activation_kind = 2; p.state.last_activation_outcome = 3;
        const auto before = p.state;
        const auto status = remote_action_status(p.state, 101);
        CHECK(status.request_id == 7); CHECK(status.controller_slot == 0); CHECK(status.kind == 5);
        CHECK(status.activation_ms == 300); CHECK(status.remaining_ms == (armed ? 299 : 0));
        CHECK(status.last_kind == 2); CHECK(status.last_outcome == 3);
        CHECK(remote_action_status(p.state, 401).remaining_ms == 0);
        CHECK(memcmp(&p.state, &before, sizeof before) == 0);
        RemoteSessionInstall plan{};
        if (all) plan.clear_root = true;
        else { plan.epoch_set[0] = true; plan.epoch[0] = 99; }
        remote_session_install(p.state, plan);
        CHECK(p.state.last_activation_kind == 2); CHECK(p.state.last_activation_outcome == 3);
        CHECK(p.state.action.phase == (armed ? RemoteActionPhase::armed : RemoteActionPhase::none));
        DeferredActionRecord transfer{};
        CHECK(remote_action_take(p.state, 400, transfer) == armed);
        if (armed) { CHECK(transfer.admin_epoch == before.action.admin_epoch); CHECK(transfer.kind == 5); }
        remote_session_clear(p.state);
        CHECK(p.state.action.phase == RemoteActionPhase::none); CHECK(p.state.last_activation_kind == 0);
        CHECK(p.state.last_activation_outcome == 0);
    }
}
