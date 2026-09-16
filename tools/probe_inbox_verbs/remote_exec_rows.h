// 7b-1: included by probe_main AFTER its existing fixture declarations. No second device context.
#pragma once
#include "firmware_remote_executor.h"
#include <array>
#include <new>

static void radmin7_seed_inbox() {
    using namespace meshroute;
    seed_inbox(0, 0);
    const uint8_t private_text[] = "PRIVATE-NEVER-RPC";
    const uint8_t channel_text[] = "CHANNEL-VISIBLE";
    CHK(g_node.inbox().record_dm(7, 42, 9, 0, private_text, sizeof(private_text) - 1, 100) != 0,
        "R7-I1 real private DM stored");
    CHK(g_node.inbox().record_ack(7, 9, 0, 101, 42) != 0, "R7-I2 real delivery receipt stored");
    CustodyFailureRecord r{};
    r.notice_flags = custody_notice_flags(CustodyRootStage::cts, true, false, false);
    r.terminal_reason = CustodyFailureReason::cascade_count;
    r.failed_origin = 5; r.failed_dst = 9; r.failed_ctr = 17; r.failed_type = 0; // ordinary application DM
    r.previous_hop = 1; r.failed_next_hop = 9; r.requeue_count = 3;
    r.alternatives_tried = 1; r.committed_hops = 1; r.remaining_hops = 4;
    std::array<uint8_t, custody_record_v1_len> wire{};
    const size_t n = pack_custody_failure(r, wire);
    CHK(n == custody_record_v1_len, "R7-I3 custody fixture uses real codec");
    CHK(g_node.inbox().record_custody_failure(3, 17, 0, wire.data(), static_cast<uint8_t>(n), 102) != 0,
        "R7-I4 real custody report stored");
    CHK(g_node.inbox().record_channel(3, 0x07000001, 0, channel_text, sizeof(channel_text) - 1, 103) != 0,
        "R7-I5 real channel post stored");
}

static void radmin7_inbox_rows() {
    using namespace mrfw;
    radmin7_seed_inbox();
    CaptureSink local;
    CHK(dispatch("pull_inbox 0 0", 14, local), "R7-I6 local router owns inbox pull");
    CHK(local.has("PRIVATE-NEVER-RPC") && local.has("CHANNEL-VISIBLE") && local.has("e2e_ack")
        && local.has("custody_failure") && local.has("inbox_end"), "R7-I7 full medium yields all four local records and end");
    std::array<uint8_t, meshroute::inbox_record_max_bytes + 1> oversize{};
    const unsigned before = g_dm_store.count();
    CHK(!g_dm_store.append(99, oversize.data(), oversize.size()) && g_dm_store.count() == before,
        "R7-I8 oversize fake append refuses atomically rather than clamping");
#if MR_FEAT_RADMIN_ACCEPT
    for (const CommandAuthority role : {CommandAuthority::remote_operator, CommandAuthority::remote_owner}) {
        const CommandContext ctx{CommandTransport::remote, role, false, 17, meshroute::console::remote_command_max_bytes, 1};
        CaptureSink remote;
        const auto r = exec_console_line("pull_inbox 0 0", 14, LineFormat::text, remote, nullptr, 0, ctx);
        CHK(r.outcome == DispatchOutcome::completed, "R7-I9 remote inbox handler completes (role=%u)", unsigned(role));
        CHK(!remote.has("PRIVATE-NEVER-RPC"), "R7-I10 private application DM hidden (role=%u)", unsigned(role));
        CHK(remote.has("e2e_ack"), "R7-I11 delivery receipt preserved (role=%u)", unsigned(role));
        CHK(remote.has("custody_failure"), "R7-I12 custody diagnostic preserved (role=%u)", unsigned(role));
        CHK(remote.has("CHANNEL-VISIBLE"), "R7-I13 channel preserved (role=%u)", unsigned(role));
        CHK(remote.has("inbox_end") && remote.has("\"count\":3"), "R7-I14 end counts visible rows only (role=%u)", unsigned(role));
        const unsigned cursor = g_dm_store.cursor, writes = g_dm_store.cursor_calls;
        remote.reset();
        (void)exec_console_line("mark_read dm 1", 14, LineFormat::text, remote, nullptr, 0, ctx);
        CHK(remote.is("{\"err\":\"mark_read\",\"msg\":\"remote_no_dm\"}\n") && g_dm_store.cursor == cursor
            && unsigned(g_dm_store.cursor_calls) == writes, "R7-I15 shared private cursor untouched (role=%u)", unsigned(role));
        if (role == CommandAuthority::remote_owner) {
            remote.reset(); const auto count = g_dm_store.count();
            (void)exec_console_line("del_msg dm 1", 12, LineFormat::text, remote, nullptr, 0, ctx);
            CHK(remote.is("{\"err\":\"del_msg\",\"msg\":\"remote_no_dm\"}\n") && g_dm_store.count() == count,
                "R7-I16 owner cannot delete a private record through remote context");
        }
        CaptureSink after; dispatch("pull_inbox 0 0", 14, after);
        CHK(after.is(local.buf), "R7-I17 context restored; local inbox bytes unchanged (role=%u)", unsigned(role));
    }
#endif
}

#if MR_FEAT_RADMIN_ACCEPT
struct Radmin7RadioFixture {
    meshroute::Identity self{}, root{}, controller[3]{};
    uint16_t ctr = 0xD0;
    uint64_t rid = 100;
    bool quiet_after_preparation = false; // action fixture: freeze debug admission, then silence frame tracing
    Radmin7RadioFixture() {
        using namespace meshroute;
        uint8_t seed[32] = {91}; identity_from_seed(self, seed);
        g_node.~Node(); g_hal.~DeviceHal(); g_iradio.~Sx1262Radio();
        g_radio = CustomSX1262{};
        new (&g_iradio) Sx1262Radio(g_radio, board_rf_instance());
        new (&g_hal) DeviceHal(g_clock, g_iradio);
        new (&g_node) Node(g_hal, 5, self.key_hash32, "target");
        g_probe_millis = 10000;
        CHK(g_iradio.begin(), "R7-A1 fake radio initialized through real device adapter");
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = 1u << 12; cfg.leaf_id = 0;
        CHK(g_node.on_init(cfg), "R7-A2 real Node initializes");
        g_node.set_crypto_identity(self.x_secret, self.ed_pub);
        auto& nv = mrprobe_nv(); nv.reset(); nv.ns_present = true; nv.rw_ok = true; rng_reset();
        mrnv::AdminIdBlob id{}; mrnv::admin_id_blob_init(id); id.seed[0] = 92;
        identity_from_seed(root, id.seed);
        CHK(mrnv::save_admin_id(id), "R7-A3 administration identity seeded through typed writer");
        mrnv::AclBlob acl{}; mrnv::acl_blob_init(acl);
        for (uint8_t slot = 0; slot < 3; ++slot) {
            seed[0] = slot + 93; identity_from_seed(controller[slot], seed);
            memcpy(acl.rec[slot].ed_pub, controller[slot].ed_pub, 32);
            acl.rec[slot].role = slot == 2 ? mrnv::kAclRoleOperator : mrnv::kAclRoleOwner;
        }
        acl.count = 3;
        CHK(mrnv::save_acl(acl), "R7-A4 ACL seeded through typed writer");
        mrfw::admin_stores_boot_report_console();
        CHK(g_node.admin_session_readiness() == AdminReadiness::ready, "R7-A5 real boot binding installed session");
        mrcon.service(); Serial.reset(); ble_reset();
    }
    void radio_drain() {
        for (unsigned i = 0; i < 32 && (g_hal.txq_depth() || g_iradio.tx_busy()); ++i) {
            g_hal.pump_tx();
            if (g_iradio.tx_busy() && g_radio.dio1_action) g_radio.dio1_action();
            g_hal.collect_tx_completion();
            meshroute::TxOutcome out{};
            while (g_hal.pop_tx_outcome(out)) g_node.on_tx_complete(out);
        }
        CHK(g_hal.txq_depth() == 0 && !g_iradio.tx_busy(), "R7-A6 HAL queue really drained to fake radio");
    }
    void learn(uint8_t slot) {
        using namespace meshroute;
        std::array<uint8_t, 64> wire{}; beacon_entry entry{};
        entry.dest = 7; entry.next = 7; entry.score_bucket = 14; entry.hops = 1;
        beacon_in b{}; b.src = 7; b.key_hash32 = controller[slot].key_hash32; b.entries = {&entry, 1};
        const size_t n = pack_beacon(b, wire);
        g_node.on_recv(wire.data(), n, RxMeta{12, -70, 0, 7});
    }
    void session_key(uint8_t slot, uint8_t (&session)[32]) {
        using namespace meshroute;
        uint8_t x[32], shared[32], base[32]; ed_pub_to_x25519(x, root.ed_pub);
        CHK(remote_ecdh_shared(shared, controller[slot], x) == RemoteStatus::ok, "R7-A7 controller ECDH");
        CHK(remote_kdf_base(base, shared, controller[slot].ed_pub, root.ed_pub) == RemoteStatus::ok, "R7-A8 ordered base KDF");
        CHK(remote_kdf_session(session, base, g_node.admin_session_state().epoch[slot]) == RemoteStatus::ok, "R7-A9 session KDF");
        crypto_wipe(x, sizeof x); crypto_wipe(shared, sizeof shared); crypto_wipe(base, sizeof base);
    }
    void flight(const char* line, uint8_t slot, bool ack = false, bool open = false,
                meshroute::RemoteCmdOpcode control = meshroute::RemoteCmdOpcode::auth_execute,
                size_t line_len = SIZE_MAX) {
        using namespace meshroute;
        learn(slot); ++ctr; g_probe_millis += 1000;
        RemoteCarrier carrier{}; carrier.outer_data_type = DATA_TYPE_REMOTE_CMD; carrier.dst_hash_on_wire = true;
        RemoteMessage msg{}; msg.outer_type = DATA_TYPE_REMOTE_CMD; msg.slot = open ? kRemoteSlotSentinel : slot;
        msg.request_id = rid;
        msg.opcode = static_cast<uint8_t>(open ? RemoteCmdOpcode::open_execute : ack ? RemoteCmdOpcode::response_ack : control);
        uint8_t session[32] = {}; if (!open) session_key(slot, session);
        std::array<uint8_t, kRadminBodyBytes> body{}; size_t body_len = 0;
        const auto plain = ack || (!open && control != RemoteCmdOpcode::auth_execute) ? std::span<const uint8_t>{}
            : std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(line), line_len == SIZE_MAX ? strlen(line) : line_len);
        CHK(remote_body_encode(body, body_len, msg, plain, open ? RemoteKeys{} : RemoteKeys{{}, session},
                               {true, controller[slot].key_hash32}, carrier)
            == RemoteStatus::ok, "R7-A10 controller encodes request with its real security domain");
        crypto_wipe(session, sizeof session);
        std::array<uint8_t, 280> inner{}, wire{};
        const uint8_t flags = DATA_FLAG_SOURCE_HASH | DATA_FLAG_DST_HASH;
        const size_t in = pack_unicast_inner(inner, flags, self.key_hash32, nullptr, 0, 0, 7,
            controller[slot].key_hash32, body.data(), body_len, 0, 0);
        data_in d{}; const uint8_t mac[4] = {};
        d.flags = flags; d.type = DATA_TYPE_REMOTE_CMD; d.next = d.dst = 5; d.hops_remaining = 31; d.ctr = ctr;
        d.inner = {inner.data(), in}; d.mac = mac;
        const size_t dn = pack_data(d, wire); CHK(dn > 0, "R7-A11 real DATA packer");
        std::array<uint8_t, 16> rb{}; rts_in r{};
        r.src = 7; r.next = r.dst = 5; r.ctr_lo = ctr & 15; r.sf_index = 3; r.payload_len = dn;
        r.id = rts_flight_identity_plain(7, ctr);
        const size_t rn = pack_rts(r, rb);
        const RxMeta meta{12, -70, 0, 7};
        g_node.on_recv(rb.data(), rn, meta); radio_drain();
        g_probe_millis += 100; g_node.on_recv(wire.data(), dn, meta);
        radio_drain(); g_node.on_timer(9); radio_drain();
    }
    void pump_response() {
        using namespace meshroute;
        const size_t first = g_radio.tx_frames.size();
        radio_drain();
        const auto rts_aired = [&]() {
            for (size_t i = first; i < g_radio.tx_frames.size(); ++i) {
                const auto r = parse_rts(g_radio.tx_frames[i]);
                if (r && r->src == 5 && r->next == 7) return true;
            }
            return false;
        };
        // The real DeviceHal draws nonzero origination jitter; the native fixture's Hal
        // draws its lower bound. Drive the real queue-wakeup timer before offering a CTS.
        const uint64_t limit = g_hal.now() + 1000;
        for (unsigned i = 0; i < 32 && !rts_aired(); ++i) {
            const uint64_t due = g_hal.next_due_ms(), now = g_hal.now();
            if (due > limit) break;
            if (due > now) g_probe_millis += static_cast<uint32_t>(due - now);
            for (unsigned j = 0; j < 64; ++j) {
                const int timer = g_hal.pop_due_timer();
                if (timer < 0) break;
                g_node.on_timer(static_cast<uint32_t>(timer));
            }
            radio_drain();
        }
        CHK(rts_aired(), "R7-A29 actual response RTS precedes controller CTS");
        const RxMeta meta{12, -70, 0, 7};
        std::array<uint8_t, 8> wire{}; cts_in c{};
        c.chosen_data_sf = 7; c.tx_id = 7; c.rx_id = 5;
        const size_t n = pack_cts(c, wire);
        g_probe_millis += 10; g_node.on_recv(wire.data(), n, meta);
        g_probe_millis += 10; g_node.on_timer(7); radio_drain();
        uint8_t lo = 0;
        for (auto it = g_radio.tx_frames.rbegin(); it != g_radio.tx_frames.rend(); ++it) {
            const auto d = parse_data(*it); if (d) { lo = d->ctr & 15; break; }
        }
        ack_in a{}; a.to = 5; a.ctr_lo = lo;
        const size_t an = pack_ack(a, wire); g_probe_millis += 10;
        g_node.on_recv(wire.data(), an, meta); radio_drain();
        CHK(!g_node.has_pending_tx(), "R7-A30 actual DATA hop ACK finishes the response flight");
    }
    std::string open_command(const char* line, size_t len, meshroute::RemoteTerminal expected, bool compare_local = false) {
        using namespace meshroute;
        // Expire the previous admission's ORIGINAL cooldown; do not bypass the rate limiter between rows.
        g_probe_millis += radmin_staging_lifetime_ms; ++rid;
        flight(line, 1, false, true, RemoteCmdOpcode::auth_execute, len);
        const auto& state = g_node.admin_session_state();
        CHK(remote_session_staging_used(state) == 1 && remote_session_seen_used(state) == 0,
            "R72-O1 open radio request owns only independent staging");
        CaptureSink local;
        if (compare_local) mrfw::dispatch(line, len, local);
        const auto inbound = state.inbound_refusal;
        const unsigned calls = g_command_calls;
        const int writes = mrprobe_nv().writes;
        g_routed[0] = 0; Serial.reset(); ble_reset();
        mrfw::remote_executor_service_once();
        CHK(remote_open_next(state) < kRadminOpenSlots, "R72-O2 real seam completed the open capture");
        std::string text; unsigned frames = 0, terminals = 0;
        for (unsigned pass = 0; pass < 12 && remote_open_next(state) != kRadminNoSlot; ++pass) {
            const auto before = g_radio.tx_frames.size();
            mrfw::remote_executor_service_once(); pump_response();
            for (size_t j = before; j < g_radio.tx_frames.size(); ++j) {
                const auto& raw = g_radio.tx_frames[j]; const auto data = parse_data(raw);
                if (!data || data->type != DATA_TYPE_REMOTE_RESP) continue;
                const auto inner = parse_unicast_inner(data_inner(raw, *data), data->flags);
                CHK(inner.has_value(), "R72-O3 actual open return carrier parses"); if (!inner) continue;
                CHK(inner->has_source_hash && inner->source_hash == self.key_hash32,
                    "R72-O4 actual carrier uses target source identity");
                RemoteCarrier carrier{}; carrier.outer_data_type = DATA_TYPE_REMOTE_RESP;
                carrier.dst_hash_on_wire = inner->has_dst_hash;
                RemoteDecoded d{}; uint8_t plain[kRadminBodyBytes] = {};
                CHK(remote_body_decode(d, DATA_TYPE_REMOTE_RESP, inner->body, {}, {true, controller[1].key_hash32}, carrier, plain)
                    == RemoteStatus::ok, "R72-O5 actual response decodes WITHOUT a key");
                CHK(d.msg.slot == kRemoteSlotSentinel && d.msg.request_id == rid && d.msg.response_seq == frames++,
                    "R72-O6 open sentinel ID sequence");
                if (d.msg.opcode == static_cast<uint8_t>(RemoteRespOpcode::output))
                    text.append(reinterpret_cast<const char*>(d.body.data()), d.body.size());
                else { ++terminals; CHK(d.terminal == expected, "R72-O7 typed open terminal"); }
            }
        }
        CHK(terminals == 1, "R72-O8 exactly one open terminal");
        if (compare_local) CHK(text == local.buf, "R72-O9 open %s is byte-identical to local at the execution snapshot", line);
        if (expected == RemoteTerminal::refused || expected == RemoteTerminal::unknown_command) {
            CHK(g_command_calls == calls && g_routed[0] == 0 && text.empty(), "R72-O10 open refusal never calls a handler");
            CHK(state.inbound_refusal == inbound + 1, "R72-O11 one staged refusal, counted once");
        } else CHK(state.inbound_refusal == inbound, "R72-O12 successful open dispatch is not an inbound refusal");
        CHK(mrprobe_nv().writes == writes, "R72-O13 open path writes no NV");
        const OpenCapture zero{};
        CHK(memcmp(&state.open[0], &zero, sizeof zero) == 0
            && state.staging[0].kind == static_cast<uint8_t>(OpenStagingKind::open_cooldown),
            "R72-O14 terminal wipes capture, retains cooldown");
        mrcon.service(); CHK(Serial.n_out == 0 && g_ble_n == 0, "R72-O15 open transcript cannot leak to USB/BLE");
        CHK(mrfw::active_command_context().authority == mrfw::CommandAuthority::local,
            "R72-O16 real command context restored after open service");
        return text;
    }

    std::string command(const char* line, uint8_t slot, meshroute::RemoteTerminal expected, bool compare_local = false,
                        bool acknowledge = true) {
        using namespace meshroute;
        ++rid; flight(line, slot);
        CHK(remote_session_seen_find(g_node.admin_session_state(), slot, rid) < kRadminSeenSlots
            && remote_session_ingress_used(g_node.admin_session_state()) == 1,
            "R7-A28 authenticated flight actually admits %s", line);
        CaptureSink local;
        if (compare_local) mrfw::dispatch(line, strlen(line), local);
        const unsigned node_calls = g_command_calls;
        g_routed[0] = 0; Serial.reset(); ble_reset();
        mrfw::remote_executor_service_once();
        if (quiet_after_preparation) meshroute::g_mr_trace_on = false;
        CHK(remote_session_ingress_used(g_node.admin_session_state()) == 0, "R7-A12 main-loop completion releases ingress");
        std::string text; unsigned frames = 0, terminals = 0;
        uint8_t session[32]; session_key(slot, session);
        for (unsigned i = 0; i < 9; ++i) {
            if (remote_transcript_next(g_node.admin_session_state()) == kRadminNoSlot) break;
            const size_t before = g_radio.tx_frames.size();
            mrfw::remote_executor_service_once(); pump_response();
            for (size_t j = before; j < g_radio.tx_frames.size(); ++j) {
                const auto& raw = g_radio.tx_frames[j]; const auto data = parse_data(raw);
                if (!data || data->type != DATA_TYPE_REMOTE_RESP) continue;
                const auto inner = parse_unicast_inner(data_inner(raw, *data), data->flags);
                CHK(inner.has_value(), "R7-A13 response has valid real carrier"); if (!inner) continue;
                CHK(inner->has_source_hash && inner->source_hash == self.key_hash32, "R7-A14 air source is target identity");
                RemoteCarrier c{}; c.outer_data_type = DATA_TYPE_REMOTE_RESP; c.dst_hash_on_wire = inner->has_dst_hash;
                std::array<uint8_t, kRadminBodyBytes> plain{}; RemoteDecoded decoded{};
                const auto s = remote_body_decode(decoded, DATA_TYPE_REMOTE_RESP, inner->body, RemoteKeys{{}, session},
                    {true, controller[slot].key_hash32}, c, plain);
                CHK(s == RemoteStatus::ok, "R7-A15 controller decodes sealed frame");
                CHK(decoded.msg.request_id == rid && decoded.msg.response_seq == frames++, "R7-A16 request and sequence match");
                if (decoded.msg.opcode == static_cast<uint8_t>(RemoteRespOpcode::output))
                    text.append(reinterpret_cast<const char*>(decoded.body.data()), decoded.body.size());
                else { ++terminals; CHK(decoded.terminal == expected, "R7-A17 typed terminal matches %u", unsigned(expected)); }
                crypto_wipe(plain.data(), plain.size());
            }
        }
        crypto_wipe(session, sizeof session);
        CHK(terminals == 1, "R7-A18 exactly one mandatory terminal aired");
        if (compare_local) CHK(text == local.buf, "R7-A19 real remote %s bytes equal local dispatch at execution time", line);
        if (expected == RemoteTerminal::refused || expected == RemoteTerminal::unknown_command)
            CHK(g_command_calls == node_calls && g_routed[0] == 0 && text.empty(), "R7-A20 refusal never calls Node/handler");
        mrcon.service();
        const std::string scalar = "> remote-action scheduled request_id=";
        CHK(g_ble_n == 0 && (expected == RemoteTerminal::scheduled
            ? std::string(Serial.out).find(scalar) == 0 && std::string(Serial.out).find("> rebooting") == std::string::npos
            : Serial.n_out == 0), "R7-A21 only scheduled scalar metadata may reach the local console");
        if (!acknowledge) return text;
        flight("", slot, true);
        const auto& state = g_node.admin_session_state();
        const uint8_t si = remote_session_seen_find(state, slot, rid);
        CHK(si < kRadminSeenSlots && state.seen[si].record.state == static_cast<uint8_t>(SeenState::acknowledged)
            && state.seen[si].record.transcript_slot == kRadminNoTranscript
            && remote_session_ingress_used(state) == 0 && remote_transcript_next(state) == kRadminNoSlot,
            "R7-A22 response ACK releases transcript, retains tombstone, and consumes no ingress/reply");
        return text;
    }
};

static void radmin7_air_rows() {
    using namespace meshroute;
    Radmin7RadioFixture f;
    f.command("status", 1, RemoteTerminal::completed, true);
    f.command("version", 1, RemoteTerminal::completed, true);
    const int writes = mrprobe_nv().writes;
    f.command("unknown-verb", 1, RemoteTerminal::unknown_command);
    f.command("factory_reset confirm", 2, RemoteTerminal::refused);
    f.command("reboot", 1, RemoteTerminal::scheduled);
    CHK(mrprobe_nv().writes == writes, "R7-A23 remote refused commands write no NV");
    CHK(f.command("acl remove 1 confirm", 1, RemoteTerminal::completed).find("self_slot") != std::string::npos,
        "R7-A24 real remote actor cannot remove itself");
    CHK(f.command("acl set 1 operator", 1, RemoteTerminal::completed).find("self_slot") != std::string::npos,
        "R7-A25 real remote actor cannot demote itself");
    CHK(f.command("acl remove 0 confirm", 1, RemoteTerminal::completed).find("removed slot=0") != std::string::npos,
        "R7-A26 real remote actor may remove another owner");
    CaptureSink local;
    mrfw::dispatch("acl set 1 operator", 18, local);
    CHK(local.has("last_owner"), "R7-A27 local behavior retains last-owner protection");
}

static void radmin72_open_rows() {
    using namespace meshroute;
    Radmin7RadioFixture f;
    auto& state = const_cast<RemoteSessionState&>(g_node.admin_session_state());
    for (const uint16_t seed : {uint16_t(0), uint16_t(11), uint16_t(UINT16_MAX)}) {
        // Labelled scalar seeding, not a claimed production failure. Native/radio cases drive the causes.
        state.inbound_refusal = seed;
        state.open_rate_refusal = seed == 11 ? 22 : seed;
        state.transcript_exhaustion = seed == 11 ? 33 : seed;
        state.response_enqueue_failure = seed == 11 ? 44 : seed;
        state.response_seal_failure = seed == 11 ? 55 : seed;
        const auto text = f.open_command("status", 6, RemoteTerminal::completed, true);
        const std::string expected = " radmin_inbound_refusal=" + std::to_string(state.inbound_refusal)
            + " radmin_open_rate_refusal=" + std::to_string(state.open_rate_refusal)
            + " radmin_transcript_exhaustion=" + std::to_string(state.transcript_exhaustion)
            + " radmin_response_enqueue_failure=" + std::to_string(state.response_enqueue_failure)
            + " radmin_response_seal_failure=" + std::to_string(state.response_seal_failure)
            + " radmin_action_phase=none radmin_action_armed=0 radmin_last_activation_kind=none radmin_last_activation_outcome=none\r\n";
        CHK(text.size() >= expected.size() && text.compare(text.size() - expected.size(), expected.size(), expected) == 0,
            "R72-S1 exact five counters and empty action status at seed %u", unsigned(seed));
        for (const char* name : {"radmin_inbound_refusal=", "radmin_open_rate_refusal=", "radmin_transcript_exhaustion=",
                                 "radmin_response_enqueue_failure=", "radmin_response_seal_failure="}) {
            const auto first = text.find(name);
            CHK(first != std::string::npos && text.find(name, first + 1) == std::string::npos,
                "R72-S2 each scalar label appears exactly once: %s", name);
        }
    }
    state.inbound_refusal = state.open_rate_refusal = state.transcript_exhaustion = 0;
    state.response_enqueue_failure = state.response_seal_failure = 0;
    f.open_command("routes", 6, RemoteTerminal::completed, true);
    for (const std::string& line : {std::string("status "), std::string("status x"),
                                  std::string("routes 1"), std::string("reboot"), std::string("acl list"),
                                  std::string("status\0x", 8), std::string("status\r", 7), std::string("status\n", 7),
                                  std::string(202, 'x')})
        f.open_command(line.data(), line.size(), RemoteTerminal::refused);
    for (const char* line : {" status", "Status", "unknown-command"})
        f.open_command(line, strlen(line), RemoteTerminal::unknown_command);

    const auto epoch = state.epoch[1]; const auto writes = mrprobe_nv().writes;
    ++f.rid; f.flight("", 1, false, false, RemoteCmdOpcode::safe_rollover);
    const auto draws = mrprobe_rng().draws;
    mrfw::remote_executor_service_once(); f.pump_response();
    CHK(state.epoch[1] != epoch && mrprobe_rng().draws == draws + 2,
        "R72-C1 actual firmware service consumes control with one eight-byte draw");
    CHK(mrprobe_nv().writes == writes && remote_session_ingress_used(state) == 0,
        "R72-C2 actual firmware control writes no NV and releases its ingress");
    CHK(mrfw::active_command_context().authority == mrfw::CommandAuthority::local,
        "R72-C3 control leaves no command context published");
}
#endif
