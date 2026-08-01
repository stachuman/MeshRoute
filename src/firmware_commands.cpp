// MeshRoute — src/firmware_commands.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The console command cluster (see firmware_commands.h) moved VERBATIM from fw_main.cpp (cleanup 2026-07-15).
// Shared device state comes from fw_context.h; the STAY-set board-glue (do_reboot/do_ota/dump_faults/
// handle_crashtest/handle_prep_restart) is reached ONLY via the fw_context.h wrappers — this TU MUST NOT include
// device_fault.h (its ISR vectors + the MRFAULT_HW/MRFAULT_ESP32 macros are single-TU). Behaviour-preserving.
#include "firmware_commands.h"
#include "fw_context.h"        // g_node + the shared state
#include "device_nv.h"         // mrnv::PeerBlob / load_peers / save_peers
#include <cstdio>              // snprintf
#include <cstring>             // memcpy
#include <cstdlib>             // strtol/strtoul (route/testsched/lookup parsing)
#include "firmware_config.h"   // dispatch re-fans-out to mrfw:: config verbs (gateway/join/create/team/mobile/leave/cfg_set)
#include "firmware_remote.h"   // dispatch: rcmd + (MR_FEAT_REMOTE_MGMT) password/unlock/lock
#include "firmware_inbox.h"    // dispatch/ble: pull_inbox / mark_read
#include "console_sink.h"      // mrcon (inline, ODR-merged) — do_regen + handlers print through it
#include "console_json.h"      // write_status/write_cfg/write_limits/write_route + StatusFields/CfgExtras
#include "frame_trace.h"       // g_mr_trace_on (handle_debug)
#include "sched_send.h"        // mrsched::Schedule (handle_testsched/teststatus + g_sched)
#include "device_rng.h"        // mrrng::fill (do_regen)

#ifndef GIT_REV
#define GIT_REV "nogit"        // tools/git_rev.py injects -DGIT_REV on the nRF52 base env only; this fallback (mirrors fw_main.cpp) keeps the ESP32 envs compiling — print_banner reads it
#endif

namespace mrfw {

// ---- the /mrpeers address book: the I/O half (§AB1, spec 2026-07-29 §2.4) --------------------------------------
// ★ ONE 1160-B SCRATCH RECORD, STATIC, SHARED BY BOTH USERS BELOW — and `static` here is a HARD requirement, not
// tidiness. /mrpeers is whole-blob R/W and v2 doubled it (584 -> 1160 B), and BOTH call sites are stack-critical:
//   - peer_store_restore() runs from setup(), i.e. on the nRF52 Arduino loop task, whose stack is a FIXED 4 KB
//     (LOOP_STACK_SZ = 256*4 in cores/nRF5/main.cpp, not overridable) — 1160 B is 28% of the WHOLE stack, and this
//     tree has already HARDFAULTed on that stack once (fw_main.cpp §stability: `stackhw` fell to 72 B);
//   - peer_store_sync() runs from the console / push drain in the 8 KB g_mesh_task, whose deepest RX path
//     (hash-locate -> RREQ flood -> do_post_ack) already nests ~1.4 KB.
// Sharing it is safe because the two CANNOT overlap: setup() returns before loop() lazily creates g_mesh_task, and
// everything after that is single-threaded cooperative on that one task. Same reason (and the same wording) as the
// `static Blob cur` in mrnv::save() and the `static mrfault::FaultLog fl` in mrnv::factory_erase().
static mrnv::PeerBlob s_peers;

// Mirror ONE peer of the live address book into /mrpeers. Was `persist_pinned_peer` (pinned-only, one record built
// field-by-field at the call site); EXTENDED rather than forked (U1). Two halves, deliberately split:
//   - the SELECTION + EVICTION policy is mrnv::peer_rec_put (device_nv.h — pure, host-tested, mirrors
//     Node::peer_key_set's rules without forking them);
//   - this function is the I/O half plus THE ONE RAM->NV conversion path (U2): every field is read from the LIVE
//     table through the existing accessors (peer_key_find -> ed_pub + the CURRENT confidence, peer_name_find -> the
//     cached name), so no caller can hand the store a name or a confidence that disagrees with RAM.
mrnv::PeerPut peer_store_sync(uint32_t kh) {
    uint8_t ed[32];
    meshroute::Node::PeerKeyConf conf = meshroute::Node::PeerKeyConf::overheard;
    if (!g_node.peer_key_find(kh, ed, &conf)) return mrnv::PeerPut::refused_absent;   // C2: absent/aged in RAM -> nothing to mirror
                                                                                    // (reachable: a peer_key_cached push is drained
                                                                                    // after the cache event, and a cache-on-pass flood
                                                                                    // can evict the entry in between)
    char nm[32];
    const uint8_t nl = g_node.peer_name_find(kh, nm, sizeof nm);
    // ★ RE-READ THE STORE EVERY TIME, deliberately — do NOT treat `s_peers` as a warm cache after the first load.
    // `factory_erase()` (a `factory_reset confirm`) wipes /mrpeers behind this function's back, and a cached copy
    // would then be written straight back over the wipe. A read is cheap and wears nothing; only writes wear flash.
    if (!mrnv::load_peers(s_peers)) mrnv::peers_blob_init(s_peers);   // absent, or a REJECTED v1 record -> start a fresh v2 store
    const mrnv::PeerPut r = mrnv::peer_rec_put(s_peers, kh, ed, (uint8_t)conf, nm, nl);
    // ★ `unchanged` => DO NOT WRITE. The wear guard is the whole reason peer_rec_put reports an outcome instead of a
    // bool: v2 made every on-air key-learn a write candidate, and a re-cache of an identical record must cost no flash.
    if (r == mrnv::PeerPut::updated || r == mrnv::PeerPut::inserted || r == mrnv::PeerPut::evicted)
        (void)mrnv::save_peers(s_peers);   // §nv-unchecked [5/5]: the save verdict is DISCARDED, preserved deliberately
                                           // (see fw_main §nv-unchecked [1/5]) — best-effort NV; the RAM key works
                                           // regardless. Owner ruling owed. The POLICY verdict `r` IS returned.
    return r;
}

// E2E §2 + §AB1: reload the address book at boot. Re-installs each stored record AT ITS STORED CONFIDENCE — v1
// re-installed EVERYTHING as `pinned`, which silently asserted "a human verified this via QR" of a key learned on
// air. An unrecognised confidence byte is SKIPPED, never guessed at (mrnv::peer_conf_restorable). Called from
// setup() AFTER on_init so the LayerRuntime `_active` is live. Returns the number of records re-installed.
uint16_t peer_store_restore() {
    uint16_t ok = 0, pinned = 0, bad = 0;
    if (mrnv::load_peers(s_peers) && s_peers.count <= mrnv::kMaxPeerRecs) {
        for (uint16_t i = 0; i < s_peers.count; ++i) {
            const mrnv::PeerRec& rec = s_peers.rec[i];
            if (!mrnv::peer_conf_restorable(rec.confidence)) { ++bad; continue; }
            const bool pin = (rec.confidence == mrnv::kPeerConfPinned);
            uint8_t nl = rec.name_len;
            if (nl > sizeof rec.name) nl = (uint8_t)sizeof rec.name;      // a bit-rotted name_len must not overrun
            if (g_node.peer_key_set(rec.key_hash32, rec.ed_pub,
                                    pin ? meshroute::Node::PeerKeyConf::pinned
                                        : meshroute::Node::PeerKeyConf::authoritative,
                                    rec.name, nl)) { ++ok; pinned += pin ? 1 : 0; }
            else ++bad;                                                   // ed_pub[:4] != hash, or the cache is full of pinned keys
        }
    }
    // One boot line: the kPeersVersion 1->2 bump makes this read `0 restored` exactly once (the v1 record is
    // rejected), so the one-time loss of the pinned store is OBSERVABLE rather than silent.
    mrcon.print(F("  peers     = ")); mrcon.print(ok);   mrcon.print(F(" restored ("));
    mrcon.print(pinned);              mrcon.print(F(" pinned, "));
    mrcon.print((uint16_t)(ok - pinned)); mrcon.print(F(" authoritative)"));
    if (bad) { mrcon.print(F(" ⚠ ")); mrcon.print(bad); mrcon.print(F(" REJECTED")); }
    mrcon.println();
    return ok;
}

// E2E §3: a `peerkey` command -> install the RAM PINNED key (Node::on_command) + mirror it to /mrpeers + the ack.
size_t handle_peerkey(char* out, size_t cap, const meshroute::Command& cmd) {
    const uint8_t* ep = cmd.u.peerkey.ed_pub;
    const uint32_t kh = (uint32_t)ep[0] | ((uint32_t)ep[1] << 8) | ((uint32_t)ep[2] << 16) | ((uint32_t)ep[3] << 24);
    if (g_node.on_command(cmd).code != meshroute::CmdCode::queued)        // false only when the cache is full of pinned keys
        return (size_t)snprintf(out, cap, "{\"ev\":\"peerkey_err\",\"reason\":\"full\"}\n");
    // §nv-unchecked [5/5] (cont.): peer_store_sync reports its POLICY outcome — this CALLER discards it, preserved
    // deliberately: the `peerkey_set` ack below claims `"pinned":true` even when /mrpeers refused or failed to write.
    // Owner ruling owed. NB the RAM install above must come FIRST — the sync reads the name + confidence back out of
    // the live table, so a QR import inherits any name already learned on air for that hash.
    (void)peer_store_sync(kh);
    return (size_t)snprintf(out, cap, "{\"ev\":\"peerkey_set\",\"hash\":%lu,\"pinned\":true}\n", (unsigned long)kh);
}

// ★ §AB2 (spec 2026-07-29 §2.3): a `peername` command -> rename the RAM entry (Node::on_command -> peer_name_set) +
// mirror it to /mrpeers + the SYNCHRONOUS ack. Deliberately the same three-step shape as handle_peerkey above (U3), and
// the persistence is AB1's ONE-LINER: peer_store_sync takes only a hash and reads EVERY field back out of the live
// table, so no caller can persist a name that disagrees with RAM (U2). The RAM write must therefore come FIRST.
// ⚠ The NV outcome is DISCARDED here for the same reason it is in handle_peerkey (§nv-unchecked [5/5]): the ack claims
// success on the strength of the RAM write, which is what the operator asked for and what every read path uses. An
// owner ruling on surfacing NV failures is still owed and covers both verbs together.
// ★ A rename of a PINNED peer SUCCEEDS — peer_name_set touches neither key nor confidence, so peer_key_set's
// "pinned is immutable to an on-air set" rule is not in play (see node.h). peer_store_sync then re-mirrors the record at
// its UNCHANGED confidence, so a pinned peer stays pinned in /mrpeers with the new name.
size_t handle_peername(char* out, size_t cap, const meshroute::Command& cmd) {
    const uint32_t kh = cmd.u.peername.key_hash32;
    const meshroute::CmdResult r = g_node.on_command(cmd);
    if (r.code != meshroute::CmdCode::queued)
        return meshroute::console::write_peer_name_err(out, cap,
                   r.code == meshroute::CmdCode::err_too_large ? "too_long" : "unknown_hash");
    (void)peer_store_sync(kh);
    return meshroute::console::write_peer_name_set(out, cap, kh,
               reinterpret_cast<const char*>(cmd.body), cmd.body_len);
}

// ---- device-console diagnostics (host tool: tools/meshroute_client.py) ---------------------------
// Print the live routing table in the meshroute_client `routes` wire format.
// `route add <dest> <next_hop> <hops> [score_q4]` / `route del <dest>` — manually force / drop a route. A TESTING lever
// to stress the routing algorithms with arbitrary or deliberately-inconsistent routes. `score_q4` is the same Q4-dB
// value the `routes` dump shows; rt_merge competes the injected candidate like any learned one (high score -> primary).
static void handle_route_cmd(const char* args, Print& out) {
    while (*args == ' ') ++args;
    char* e;
    if (!strncmp(args, "add", 3) && (args[3] == ' ' || args[3] == '\0')) {
        const char* p = args + 3;
        const long dest = strtol(p, &e, 10); const bool d1 = (e != p); p = e;
        const long next = strtol(p, &e, 10); const bool d2 = (e != p); p = e;
        const long hops = strtol(p, &e, 10); const bool d3 = (e != p); p = e;
        long score = strtol(p, &e, 10); if (e == p) score = 160;          // optional; default ~10 dB (Q4) = a sticky primary
        if (d1 && d2 && d3 && dest >= 1 && dest <= 254 && next >= 1 && next <= 254 && hops >= 1 && hops <= 255) {
            const bool ok = g_node.route_inject((uint8_t)dest, (uint8_t)next, (uint8_t)hops, (int16_t)score);
            out.print(F("> route add dest=")); out.print(dest); out.print(F(" via=")); out.print(next);
            out.print(F(" hops="));            out.print(hops); out.print(F(" score=")); out.print(score);
            out.println(ok ? F(" — installed (see `routes`)") : F(" — REJECTED (better candidates hold the slots)"));
            return;
        }
    } else if (!strncmp(args, "del", 3) && (args[3] == ' ' || args[3] == '\0')) {
        const long dest = strtol(args + 3, &e, 10);
        if (e != args + 3 && dest >= 1 && dest <= 254) {
            const bool ok = g_node.route_remove((uint8_t)dest);
            out.print(F("> route del dest=")); out.print(dest); out.println(ok ? F(" — removed") : F(" — not found"));
            return;
        }
    }
    out.println(F("> route err usage: route add <dest> <next_hop> <hops> [score_q4] | route del <dest>"));
}

static void dump_routes(Print& out) {
    const uint64_t now = g_hal.now();
    //out.print(F("[routes] n=")); out.println(g_node.rt_count());
    if (!g_node.rt_count()) out.println(F("empty"));
    for (uint8_t i = 0; i < g_node.rt_count(); ++i) {
        const meshroute::RtEntry& e = g_node.rt_at(i);
        const meshroute::RtCandidate& c = e.candidates[0];           // candidates[0] = the primary next-hop
        out.print(F("[route] dest="));   out.print(e.dest);
        out.print(F(" next="));          out.print(c.next_hop);
        out.print(F(" hops="));          out.print(c.hops);
        out.print(F(" score="));         out.print(c.score);
        out.print(F(" pen="));           out.print(g_node.peer_penalty_q4(c.next_hop));   // liveness penalty on this next-hop (effective = score - pen)
        out.print(F(" gw="));            out.print(c.is_gateway ? 1 : 0);
        out.print(F(" leaf="));          out.print(c.learned_leaf);
        out.print(F(" age_ms="));        out.print((uint32_t)(now - c.last_seen_ms));
        out.print(F(" cand="));          out.println(e.n);
        // A gateway route carries unique state: its advertised window schedule (period + per-leaf windows) — known
        // when we've heard the gateway 1-hop. Print it on a continuation line so a node can see when the gw is reachable.
        if (c.is_gateway) {
            const meshroute::GatewaySchedule* gs = g_node.rt_gateway_schedule(e.dest);
            if (gs && gs->valid) {
                out.print(F("[route]   gw_sched period="));  out.print(gs->period_ms);
                out.print(F("ms heard_ms="));                out.print((uint32_t)(now - gs->heard_ms));
                out.print(F(" defer_ms="));                  out.print(g_node.rt_gateway_defer_ms(e.dest));
                out.print(F(" n_rec="));                     out.print(gs->n_rec);
                for (uint8_t r = 0; r < gs->n_rec; ++r) {
                    out.print(F(" [leaf"));   out.print(gs->rec[r].leaf_id);
                    out.print(F(" win"));     out.print(gs->rec[r].window_ms);
                    out.print(F("@"));        out.print(gs->rec[r].offset_ms);
                    out.print(F("]"));
                }
                out.println();
            } else {
                out.println(F("[route]   gw_sched unknown (not heard 1-hop)"));
            }
        }
    }
    //out.println(F("[routes] end"));
    // §mobile 6.2: the TEAM-plane routing table (_rt_team) — same RtEntry fields as the static plane; team routes are never
    // gateways (no gw schedule). Printed for ANY team member (team_id!=0) EVEN WHEN EMPTY (n=0) so the operator can tell a
    // team member with no peers-yet (a PHY mismatch / just-joined) from a non-team node — a static/non-team node prints nothing.
#if MR_FEAT_TEAM   // §featuresplit: the diagnostic is compiled out on a static-only build (no _rt_team)
    if (g_node.config().team_id != 0) {
        char tx[9]; snprintf(tx, sizeof tx, "%08lX", (unsigned long)g_node.config().team_id);
        out.print(F("team_id=0x")); out.print(tx);
        out.print(F(" team_local_id=")); out.print(g_node.team_local_id());
        out.print(F(" n=")); out.println(g_node.rt_team_count());
        for (uint8_t i = 0; i < g_node.rt_team_count(); ++i) {
            const meshroute::RtEntry& e = g_node.rt_team_at(i);
            const meshroute::RtCandidate& c = e.candidates[0];       // candidates[0] = the primary next-hop
            out.print(F("[team-route] dest="));   out.print(e.dest);
            out.print(F(" next="));                out.print(c.next_hop);
            out.print(F(" hops="));                out.print(c.hops);
            out.print(F(" score="));               out.print(c.score);
            out.print(F(" leaf="));                out.print(c.learned_leaf);
            out.print(F(" age_ms="));              out.print((uint32_t)(now - c.last_seen_ms));
            out.print(F(" cand="));                out.println(e.n);
        }

    }
#endif   // MR_FEAT_TEAM
    // §mobile: the hosted-mobile registry (this node is a HOME) — mobiles reachable by a DIRECT last-mile from here.
    // mobile_reg_count()==0 on a non-host / static build -> nothing printed.
    if (g_node.mobile_reg_count()) {
        out.print(F("hosted-mobiles n=")); out.println(g_node.mobile_reg_count());
        for (uint8_t i = 0; i < g_node.mobile_reg_count(); ++i) {
            uint32_t kh = 0; uint8_t lid = 0; bool hpk = false;
            if (!g_node.mobile_reg_at(i, kh, lid, hpk)) continue;
            char hx[9]; snprintf(hx, sizeof hx, "%08lX", (unsigned long)kh);
            out.print(F("[hosted-mobile] hash=0x")); out.print(hx);
            out.print(F(" local_id="));              out.print(lid);
            out.print(F(" pubkey="));                out.println(hpk ? F("yes") : F("no"));
        }
    }
}

// allowed_sf_bitmap -> "7,12" CSV (SF index = bit position). 0 = unconfigured.
void print_sf_list(uint16_t bitmap) {
    bool first = true;
    for (uint8_t sf = 5; sf <= 12; ++sf)
        if (bitmap & (1u << sf)) { if (!first) mrcon.print(','); mrcon.print(sf); first = false; }
    if (first) mrcon.print('-');
}

static void dump_cfg(Print& out) {
    const meshroute::NodeConfig& c = g_node.config();
    // Grouped, one section per line — readable on a raw serial monitor. Keys match the `cfg set <key>` names.
    out.print(F("node_id="));     out.println(g_node.node_id());
    const double show_freq = (c.is_mobile && c.layers[0].freq_mhz > 0.0) ? c.layers[0].freq_mhz : g_freq_mhz;   // §mobile: a retune stores the live freq in layers[0]; g_freq_mhz stays the boot/global
    out.print(F("  radio : freq="));    out.print(show_freq, 4);
    out.print(F(" routing_sf="));       out.print(c.routing_sf);
    out.print(F(" sf_list="));          print_sf_list(c.allowed_sf_bitmap);
    out.print(F(" bw="));               out.print(g_node.active_bw_hz() / 1000.0, 2); out.print(F(" kHz"));   // W2b: kHz, matching `cfg set bw`. The ACTIVE leaf's BW (a gateway alternates per window; single-layer == the global)
    out.print(F(" cr="));               out.print((int)g_node.active_cr());
    out.print(F(" tx_power="));         out.println((int)g_tx_power);
    out.print(F("  proto : duty="));    out.print(c.duty_cycle * 100.0, 2); out.print('%');   // W2b: percent, matching `cfg set duty`
    out.print(F(" beacon_ms="));        out.print(c.beacon_period_ms);
    out.print(F(" hop_cap="));          out.print(c.dv_hop_cap);
    out.print(F(" team_hop_cap="));     out.print(c.team_hop_cap);   // §team-parity T3: printed beside its static twin (unconditionally, like hop_cap) so the two radii are readable as a PAIR — they still DIFFER on the DV leg (node_beacon.cpp:884), which is exactly why an operator needs to see both
    out.print(F(" lbt="));              out.print(c.lbt_enabled ? 1 : 0);
    out.print(F(" nav="));              out.print(c.nav_enabled ? 1 : 0);
    out.print(F(" intra_relay="));      out.print(c.intra_layer_relay ? 1 : 0);   // §gateway: relay same-leaf DMs? (default OFF)
    out.print(F(" host_mobiles="));     out.print(c.host_mobiles ? 1 : 0);        // §mobile 2a: accept/host mobiles? (default ON)
    out.print(F(" nav_ignore="));       out.println(c.nav_ignore_rts ? 1 : 0);
    out.print(F("  aspam : active_fraction=")); out.print(c.channel_active_fraction, 3);   // anti-spam v2 promoted knobs (in the config_hash)
    out.print(F(" ch_min_ms="));        out.print(c.channel_min_interval_ms);
    out.print(F(" dm_min_ms="));        out.println(c.dm_min_interval_ms);
    out.print(F("  layer : "));                                            // R6.3 §3: the full 1..255 layer id (NV-side) + its wire leaf nibble (clash check)
    { mrnv::Blob lb{}; if (mrnv::load(lb) && lb.layer0_id) { out.print(F("layer=")); out.print(lb.layer0_id); out.print(F(" ")); } }
    out.print(F("leaf="));              out.print(c.leaf_id);            // leaf = layer & 0x0F (the byte-0 wire filter)
    out.print(F(" gateway="));          out.print(c.is_gateway ? 1 : 0);
    out.print(F(" gateway_only="));     out.print(c.gateway_only ? 1 : 0);
    out.print(F(" mobile="));           out.println(c.is_mobile ? 1 : 0);
    if (c.team_id) {   // §mobile 6.1/6.4: team plane — the team scope + OUR id on it. team_local_id 0 = not team-DAD'd yet
        char tx[9]; snprintf(tx, sizeof tx, "%08lX", (unsigned long)c.team_id);
        const uint8_t tid = g_node.team_local_id();
        out.print(F(" team=0x")); out.print(tx);
        out.print(F(" team_local_id=")); out.print(tid);
        // §6.4 Option X: off-grid the team-DAD'd id IS node_id (the mobile link-layer carries team DMs). Flag the plane state
        // so a bench operator can tell an off-grid member (node_id==team id) from a dual one (static node_id + separate team id).
        if (tid == 0)                       out.print(F(" (team-DAD pending)"));
        else if (g_node.node_id() == tid)   out.print(F(" (off-grid: node_id==team id)"));
        else                                out.print(F(" (dual: static node_id + team id)"));
        // §team-ch-key (T-K1): the CONTENT-key lock state (spec §2.5 "surface the lock state per team"). Boolean
        // ONLY — the pair itself is a SECRET and is deliberately NOT printable here. It is also the ONLY on-metal
        // observable that `team new` actually minted, since the corpus cannot reach the console (see the COVERAGE
        // note in the slice report). An EXPORT for the T-K4 QR is a separate, deliberately-unbuilt decision.
        out.print(F(" team_ch_key=")); out.print(g_node.team_channel_key_present() ? 1 : 0);
        out.println();
    }
    if (c.is_mobile) {                                                        // §mobile: registration state (bench diagnostic) — did we register, and with whom?
        const uint8_t h = g_node.mobile_home_id();
        out.print(F("  mobile-reg: ")); if (h) { out.print(F("REGISTERED home=")); out.println(h); } else out.println(F("UNREGISTERED (scanning)"));
    }
    if (g_node.mobile_reg_count()) { out.print(F("  hosting=")); out.print(g_node.mobile_reg_count()); out.println(F(" mobile(s)")); }   // §mobile: this node is a host with registered mobiles
    out.print(F("  member: lineage_id=")); out.print(c.lineage_id);          // R6.1 leaf-config membership: 0 = UNMANAGED. A managed leaf (lineage!=0) only routes same-lineage peers -> a lineage-0 gateway is silently dropped (node_beacon.cpp:462). This is the field to compare across nodes.
    out.print(F(" config_epoch="));     out.print(c.config_epoch);
    if (c.leaf_name_len) { out.print(F(" leaf_name=\"")); for (uint8_t i = 0; i < c.leaf_name_len; ++i) out.print(c.leaf_name[i]); out.print(F("\"")); }
    out.println();
    out.print(F("  ble   : ble_mode=")); out.print(g_ble_mode == 0 ? F("off") : g_ble_mode == 1 ? F("on") : F("periodic"));
    out.print(F(" ble_period="));       out.print(g_ble_period_min);
    out.print(F(" ble_pin="));          out.println(g_ble_pin);
    // Arduino Print formats floats via its own dtostrf (NOT newlib printf), so 7-decimal degrees print fine.
    // §loc-per-send (2026-07-31): `loc_dm=` is GONE from this dump — the toggle it reported no longer exists. Location is
    // a PER-SEND `-l` flag on `send`, so there is no persistent state to show; lat/lon below are still the node's fix.
    out.print(F("  loc   : e2e_dm="));  out.print(c.e2e_dm ? 1 : 0);
    out.print(F(" team_channel_crypt=")); out.print(c.team_channel_crypt ? 1 : 0);   // §chan-crypt CL2a: seal a `-t` post by default when a key is held. Read together with `team_ch_key=` below: key+crypt = encrypted, key+!crypt = opted out, !key = cannot encrypt at all
    out.print(F(" intro_attach="));     out.print(c.intro_attach ? 1 : 0);
    out.print(F(" lat="));              out.print(g_lat_e7 / 1e7, 7);
    out.print(F(" lon="));              out.println(g_lon_e7 / 1e7, 7);
    // Dual-layer gateway: an ADDITIVE second line per leaf (single-layer dump above is unchanged). Prints each
    // leaf's node_id/layer_id/routing_sf + the (possibly on_init-derived) window_ms/offset of the active config.
    if (c.n_layers == 2) {
        for (uint8_t li = 0; li < 2; ++li) {
            const meshroute::LayerConfig& L = c.layers[li];
            out.print(F("[cfg.layer")); out.print(li);
            out.print(F("] node_id="));    out.print(L.node_id);
            out.print(F(" layer_id="));    out.print(L.layer_id);
            out.print(F(" routing_sf="));  out.print(L.routing_sf);
            out.print(F(" sf_list="));     print_sf_list(L.allowed_sf_bitmap);
            out.print(F(" bw="));          out.print((L.bw_hz > 0 ? L.bw_hz : c.radio_bw_hz) / 1000.0, 2); out.print(F(" kHz"));   // W2b: kHz, matching `cfg set l1_bw`. Per-layer BW (0 = inherit -> the effective/global)
            out.print(F(" cr="));          out.print((int)(L.cr > 0 ? L.cr : c.radio_cr));
            out.print(F(" beacon_ms="));   out.print(L.beacon_period_ms);
            out.print(F(" window_period_ms=")); out.print(L.window_period_ms);
            out.print(F(" window_ms="));   out.print(L.window_ms);
            out.print(F(" window_offset_ms=")); out.println(L.window_offset_ms);
        }
    }
}

const char* board_name() {
#if defined(BOARD_XIAO_WIO_SX1262)
    return "xiao_nrf52";
#elif defined(BOARD_XIAO_ESP32S3)
    return "xiao_esp32s3";
#elif defined(BOARD_HELTEC_V3)
    return "heltec_v3";
#else
    return "native";
#endif
}
// The `version` banner — build stamp + git rev + board + the last reset reason (ON DEMAND, no reset). Refactored
// from the old boot prints so setup() and the `version` command share one source. (spec 2026-06-24 §6)
void print_banner(Print& out) {
    char buf[160];
    mrfault::format_version_banner(buf, sizeof buf, __DATE__ " " __TIME__, GIT_REV, board_name());
    out.println(buf);
    mrfault::format_last_reset(g_last_reset_valid ? &g_last_reset : nullptr, buf, sizeof buf);
    out.println(buf);
}

static void dump_status(Print& out) {
    out.print(F("uptime_ms="));  out.print((uint32_t)g_hal.now());
    out.print(F(" rx="));                 out.print(g_rx_count);
    out.print(F(" tx="));                 out.print(g_iradio.tx_count());
    out.print(F(" isr="));                out.print(g_iradio.isr_count());   // DIO1 edges — isr=0 ⇒ pin/mask; isr>0 & rx=0 ⇒ drain/re-arm
    out.print(F(" rxbad="));              out.print(g_iradio.rxbad_count());  // failed-decode RX (CRC storm) — a clean counter delta (per-event print is `debug on`-gated)
    out.print(F(" rxarm="));              out.print(g_iradio.rx_arm_failures());  // L5: startReceive() re-arm failures — non-zero = an SPI glitch left RX transiently un-armed (was silent before)
    out.print(F(" txq="));                out.print(g_hal.txq_depth());      // async-TX queue depth (should idle at 0)
    out.print(F(" txdrop="));             out.print(g_hal.txq_drops());      // outbound-queue overflow drops (should stay 0)
    out.print(F(" txto="));               out.print(g_hal.tx_timeouts());    // TX-watchdog recoveries — a missed TxDone (should stay 0)
    out.print(F(" slept="));              out.print(g_sleep_count);          // idle light-sleep entries — climbs = the gate fires (0 = never sleeps)
    out.print(F(" sleep="));              out.print(g_force_sleep ? F("forced") : (g_host_present ? F("off-host") : F("auto"))); // policy: auto=headless→sleeps, off-host=awake (host seen), forced=`sleep` cmd
    out.print(F(" lbt="));                out.print(g_node.config().lbt_enabled ? 1 : 0);
    out.print(F(" nf="));                 out.print(g_iradio.noise_floor(), 0); // LBT noise floor (dBm)
    out.print(F(" duty_ms="));            out.print((uint32_t)g_hal.airtime_used_ms(3600000));
    out.print(F(" routes="));             out.print(g_node.rt_count());
    out.print(F(" pending="));            out.print(g_node.has_pending_tx() ? 1 : 0);
    out.print(F(" reset="));                                                     // v2: the fault-log's newest CAUSE ("-" = none)
    if (g_last_reset_valid) out.print(mrfault::fault_cause_str(g_last_reset.cause));
    else                    out.print('-');
    out.print(F(" halted="));             out.print(g_halted ? 1 : 0);        // prep-restart: 1 = intentionally dormant, not wedged
#if defined(NRF52_PLATFORM) || defined(ARDUINO_ARCH_NRF52)
    out.print(F(" stackhw="));            out.print(loop_stack_free_bytes()); // ADDENDUM 4: loop-task min free stack bytes — the jump-to-0x0 was this overflowing; must stay well >0
#endif
    if (g_fs_reformatted) out.print(F(" fs=REFORMATTED"));                        // Part 2: InternalFS was corrupt this boot -> reformatted (re-provision)
#if defined(NRF52_PLATFORM) && defined(PIN_VBAT) && !defined(MR_NO_BATT)
    // Battery diagnostic. VBAT (P0.31) reads the CELL through a ÷3 divider — NEVER USB's 5 V (max ~4.2 V).
    // Verify vs a multimeter on the battery: mv = raw × ADC_MULTIPLIER(3.0) × AREF_VOLTAGE(3.0) / 4.096.
    pinMode(VBAT_ENABLE, OUTPUT); digitalWrite(VBAT_ENABLE, LOW);
    analogReadResolution(12); analogReference(AR_INTERNAL_3_0);
    const int braw = analogRead(PIN_VBAT);
    out.print(F(" batt_raw="));           out.print(braw);
    out.print(F(" batt_mv="));            out.print((int)((braw * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096f));
#endif
    out.println();
}

// `duty` — duty-cycle consumption readout: 0..100% of the rolling-window budget (100 = the node must stay silent),
// + when at 100% how long until airtime ages back in. `disabled` when there is no duty limit.
static void dump_duty(Print& out) {
    const auto d = g_node.duty_status();
    if (!d.enabled) { out.println(F("disabled (no duty limit)")); return; }
    out.print(d.pct); out.print('%');
    if (d.pct >= 100) { out.print(F(" — SILENT, ~")); out.print((d.avail_ms + 500) / 1000); out.print(F(" s to availability")); }
    out.println();
}

// "7,12" -> allowed_sf_bitmap (bit per SF index 5..12); 0 if none valid.
// parse_sf_list moved to firmware_config_parse.h (pure, native-tested); `using mrfw::parse_sf_list` above.

// Apply the RADIO operating point from the (just-saved) NV blob LIVE — no reboot. `reconfig` re-tunes the
// radio (freq/SF/BW/CR changed); a tx_power-only change skips the re-tune (it's set per-TX via the Hal).
// apply_radio_live moved to firmware_config.{h,cpp} (cleanup 2026-07-14, Increment A); `using mrfw::apply_radio_live` (top).

// Print the node's key_hash32 (hex, from g_identity) + name (from /mrid). Shared by boot, `status`, `regen`.
void print_identity(const mrnv::IdBlob& idb) {
    char hx[9];
    snprintf(hx, sizeof hx, "%08lX", (unsigned long)g_identity.key_hash32);
    mrcon.print(F("  key_hash32= 0x")); mrcon.print(hx);
    if (idb.name_len > 0 && idb.name_len <= sizeof idb.name) {
        mrcon.print(F("  name=\""));
        for (uint16_t i = 0; i < idb.name_len; ++i) mrcon.print(idb.name[i]);
        mrcon.print(F("\""));
    }
    mrcon.println();
}

// `regen` — mint a NEW identity (fresh HW-RNG seed) -> persist /mrid -> re-derive -> re-seed the node's
// self binding. Keeps `name` + `node_id` (the short address is independent of the keypair). The new
// key_hash32 propagates on the next beacon; peers re-bind by it (the old one ages out of their id_bind).
static void do_regen() {
    mrnv::IdBlob idb{};
    mrnv::load_id(idb);                                          // preserve the existing name (if any)
    mrrng::fill(idb.seed, sizeof idb.seed);
    idb.magic = mrnv::kIdMagic; idb.version = mrnv::kIdVersion;
    if (!mrnv::save_id(idb)) { mrcon.println(F("> regen err nv_save_failed")); return; }
    meshroute::identity_from_seed(g_identity, idb.seed);
    g_node.set_identity(g_node.node_id(), g_identity.key_hash32);
    g_node.set_crypto_identity(g_identity.x_secret, g_identity.ed_pub);   // DP1: re-install the E2E crypto identity
    mrcon.print(F("> regen ok"));
    print_identity(idb);
}

// `factory_reset` — confirm-gated full NV wipe -> reboot factory-fresh (default config + a NEW identity + no peers
// + empty inbox). The literal `confirm` token guards against an accidental paste (irreversible).
static void handle_factory_reset(const char* arg, size_t n, Print& out) {
    while (n && *arg == ' ') { ++arg; --n; }
    if (n == 7 && !strncmp(arg, "confirm", 7)) {
        out.println(F("> factory reset — erasing all NV, rebooting…"));
        g_inbox_dm.wipe(); g_inbox_ch.wipe();   // §5: drop the QSPI inbox RECORDS (their store's domain); factory_erase does the InternalFS slots + meta
        if (!mrnv::factory_erase()) out.println(F("> factory_reset WARN: an NV slot did not erase (boot re-defaults it)"));
        fw_reboot();
    } else {
        out.println(F("> factory_reset WIPES ALL flash (config + identity + peers + inbox) and reboots to factory. Type 'factory_reset confirm' to proceed."));
    }
}

// `sleep` / `sleep on` -> light-sleep when idle even though a host is present (the explicit override the user
// asked for); `sleep off` -> cancel it, stay awake. A headless node (no console byte this boot) light-sleeps
// on its own — this command is only for a node you're connected to. After `sleep on` the console goes quiet
// (light-sleep gates the UART) — reconnect to get it back (DTR resets the board). The node still wakes on RX
// (a peer DM prints RECV) and on its scheduled timers. No-op on -DMR_NO_POWERSAVE builds (the gate is gone).
static void handle_sleep(const char* arg, size_t n, Print& out) {
    while (n && *arg == ' ') { ++arg; --n; }
    if (n >= 3 && !strncmp(arg, "off", 3)) {
        g_force_sleep = false;
        out.println(F("> sleep off — staying awake while a host is connected"));
    } else {
        g_force_sleep = true;
        out.println(F("> sleep on — light-sleeping when idle; reconnect to wake the console (still wakes on RX)"));
    }
}

// `debug on` / `debug off` (also `debug 1`/`debug 0`) — gate the decoded per-frame «rx/»tx console trace
// (frame_trace.h g_mr_trace_on). §3: default OFF at boot; `debug on` enables it for the session.
static void handle_debug(const char* arg, size_t n, Print& out) {
    while (n && *arg == ' ') { ++arg; --n; }
    const bool off = (n >= 3 && !strncmp(arg, "off", 3)) || (n >= 1 && arg[0] == '0');
    meshroute::g_mr_trace_on = !off;
    out.println(off ? F("> debug off — RX/TX frame trace silenced") : F("> debug on — tracing RX/TX frames"));
}

// `lookup <hash>` — local id_bind cache peek (NO airtime): resolve a key_hash32 -> node short-id from what
// this node already knows (beacons / prior H answers). Hash is hex (e.g. `lookup 8a3f1c02`). For a network
// resolve of an unknown hash, use `resolve` (floods H).
static void handle_lookup(const char* arg, size_t n, Print& out) {
    while (n && *arg == ' ') { ++arg; --n; }
    if (n < 3 || arg[0] != '0' || (arg[1] != 'x' && arg[1] != 'X')) { out.println(F("> lookup err: hash must be 0x-prefixed (e.g. lookup 0x8a3f1c02)")); return; }
    const uint32_t hash = (uint32_t)strtoul(arg + 2, nullptr, 16);   // 0x-only (kills id-vs-hash ambiguity)
    meshroute::Node::IdBindConf conf = meshroute::Node::IdBindConf::claimed;
    const int id = g_node.id_bind_find_by_hash(hash, &conf);
    out.print(F("[lookup] 0x")); out.print(hash, HEX);
    if (id < 0) { out.println(F(" -> miss")); return; }
    out.print(F(" -> id=")); out.print(id);
    out.println(conf == meshroute::Node::IdBindConf::authoritative ? F(" (authoritative)") : F(" (claimed)"));
}

// §1.3 `nameof 0x<hash>` — the cached human name for a peer's key_hash32 (learned via the pubkey exchange, refreshed on each).
// ★ §AB3: reads the GENERATED address-book view (spec §2.1), not peer_name_find alone, so it also reports which
// namespace(s) that hash answers to. `nameof`, `hashof` and `peers` now share ONE read path by construction — the
// §2.5 defect was three verbs each reading one table and disagreeing about one identity.
static void handle_nameof(const char* arg, size_t n, Print& out) {
    while (n && *arg == ' ') { ++arg; --n; }
    if (n < 3 || arg[0] != '0' || (arg[1] != 'x' && arg[1] != 'X')) { out.println(F("> nameof err: hash must be 0x-prefixed (e.g. nameof 0x8a3f1c02)")); return; }
    const uint32_t hash = (uint32_t)strtoul(arg + 2, nullptr, 16);
    meshroute::Node::PeerBookRow row{};
    (void)g_node.peer_book_by_hash(hash, row);   // false = nothing known -> the row is zeroed and every field omits
    // §S6: JSON answer {"ev":"peer_name","hash":<dec u32>[,"name":"…"][,"static_id":N][,"team_id":N]} — app-facing query verb.
    const size_t m = meshroute::console::write_peer_name(s_inbox_jb, sizeof s_inbox_jb, hash, row.name, row.name_len,
                                                        row.static_id, row.team_id);
    if (m) out.write(s_inbox_jb, m);
}

// One `[hashof]` answer line. `queried` is what the operator typed; `row.team_id` may DIFFER when two team ids alias one
// hash — that is the ambiguity spec §2.1 forbids resolving silently, so the alias is named on the line.
static void hashof_print_row(Print& out, uint8_t queried, bool team_plane, const meshroute::Node::PeerBookRow& row) {
    out.print(F("[hashof] id=")); out.print(queried);
    out.print(team_plane ? F(" team -> 0x") : F(" static -> 0x")); out.print(row.hash, HEX);
    if (row.name_len) { out.print(F(" name=\"")); out.write(row.name, row.name_len); out.print('"'); }
    if (team_plane && row.team_id && row.team_id != queried) {          // freshest-wins picked a DIFFERENT id for this hash
        out.print(F(" (ALIASED: team id ")); out.print(row.team_id); out.print(F(" is FRESHER for this hash)"));
    }
    if (row.team_alias_dropped) { out.print(F(" +")); out.print(row.team_alias_dropped); out.print(F(" stale team-id alias dropped")); }
    out.println();
}

// `hashof <id> [-t|-s]` — an id -> its key_hash32, answered from the VIEW (spec §2.5).
// ★★ THE DEFECT THIS FIXES, bench-proven 2026-07-30: `reqpubkey <id>` reads the TEAM key cache and `hashof <id>` read
// only the STATIC _id_bind, so `reqpubkey 228` cached 0x6C297145 and `hashof 228` still said `unknown`. Each verb was
// correct about its own table; neither answered the question. Now BOTH namespaces are searched and the matching plane is
// NAMED — and when one number matches in both (the §18 dual-identity space) BOTH lines print, never one silently chosen.
// ⚠⚠ The tempting repair — writing the team hash into _id_bind — is FORBIDDEN (spec §2.5): that is the plane-blind
// ingest closed on 2026-07-31 (§id-bind-plane), an I2 breach. The fix is a READ of the view; nothing is written here.
// `-t` / `-s` narrow the search when the caller already knows the plane (the `send -t` / `reqpubkey -t` idiom, U3).
static void handle_hashof(const char* arg, size_t n, Print& out) {
    while (n && *arg == ' ') { ++arg; --n; }
    if (!n) { out.println(F("> hashof err bad_args (id 1..254 [-t|-s])")); return; }
    bool only_team = false, only_static = false;
    for (size_t i = 0; i + 1 < n; ++i)
        if (arg[i] == '-') { if (arg[i + 1] == 't') only_team = true; else if (arg[i + 1] == 's') only_static = true; }
    if (only_team && only_static) { out.println(F("> hashof err bad_args (-t and -s are exclusive)")); return; }
    const int id = atoi(arg);
    if (id < 1 || id > 254) { out.print(F("[hashof] id=")); out.print(id); out.println(F(" -> unknown (id must be 1..254)")); return; }
    meshroute::Node::PeerBookRow st{}, tm{};
    const uint8_t mask = g_node.peer_book_by_id((uint8_t)id, st, tm);
    const bool show_static = (mask & meshroute::Node::kPeerBookStatic) && !only_team;
    const bool show_team   = (mask & meshroute::Node::kPeerBookTeam)   && !only_static;
    if (show_static) hashof_print_row(out, (uint8_t)id, /*team_plane=*/false, st);
    if (show_team)   hashof_print_row(out, (uint8_t)id, /*team_plane=*/true,  tm);
    if (!show_static && !show_team) {
        out.print(F("[hashof] id=")); out.print(id);
        out.print(F(" -> unknown"));
        if (only_team)        out.print(F(" (team plane; drop -t to search both)"));
        else if (only_static) out.print(F(" (static plane; drop -s to search both)"));
        // ★ §err-reason/B33: the old text here said "try `reqpubkey`" and that advice was CIRCULAR. So name the two
        // remedies that actually work with no hash in hand: a beacon (the on-air source of the binding), or an
        // out-of-band QR import, which needs no prior hash. U3: same shape as `team grantkey`'s already-correct
        // unheard-target refusal in firmware_config.cpp. ⓘ The sibling `-t`/`-s` lines above are correct — untouched.
        // ⚠ V1 2026-08-01 (§id-hash S1): the old wording explained the circularity as *"`reqpubkey <bare id>` means a
        // team_local_id, so it resolves through team_key_of_id"* — TRUE THEN, STALE NOW. S1 made `reqpubkey <id>` read
        // `peer_book_by_id`, i.e. THIS VERY VIEW, on BOTH planes. The advice is unchanged and now holds for a stronger
        // reason: the two verbs share ONE resolver, so `reqpubkey <id>` cannot succeed where `hashof <id>` just failed.
        else                  out.print(F(" (neither plane — no beacon heard, so its key_hash32 is unknown. Remedy: wait for / provoke a beacon from it, or import its QR out-of-band with `peerkey <hex64>`. ⚠ NOT `reqpubkey <id>` — since §id-hash S1 it resolves through THIS SAME view, so it refuses with err_no_binding for the same reason; only `reqpubkey 0x<hash>` works, and the hash is what is missing)"));
        out.println();
    }
}

// ★★ §id-hash S1 (spec 2026-08-01 §1-A): a refused `reqpubkey` gets the SAME quality of remedy text handle_hashof
// above gives — the bare CmdCode (`err_no_binding` / `err_ambiguous_plane`) names the wall, not the way round it, and
// the way round it is DIFFERENT per refusal. Deliberately in this TU and not in fw_main (U3: fw_main is board/runtime
// glue; the wording belongs next to handle_hashof's so a future edit sees both).
// ★ §id-hash S1b (QA P1c) EXTENDS IT to the three send-side refusals emit_hash_query can now report, and those need it
// MORE than the resolution ones: `err_no_gateway` and `err_too_large` are REUSED codes (U1), so on this verb the token
// alone is actively confusing — nobody reads "no gateway" as "your mobile has no home to answer through". They apply to
// the by-HASH form too, so they are handled before the id-only arms.
void print_reqpubkey_hint(Print& out, const meshroute::Command& cmd, const meshroute::CmdResult& r) {
    if (cmd.kind != meshroute::CmdKind::reqpubkey) return;
    const uint8_t id = cmd.u.resolve.dst_id;
    if (r.code == meshroute::CmdCode::err_no_identity) {
        out.println(F("> reqpubkey: this node holds NO crypto identity, and the exchange is MUTUAL (our own ed_pub rides"
                      " the query), so nothing was aired. Remedy: provision an identity (`regen`), then retry."));
        return;
    }
    if (r.code == meshroute::CmdCode::err_no_gateway) {
        out.println(F("> reqpubkey: nothing aired — this node is an UNREGISTERED mobile, so its id is a LOCAL id the"
                      " owner has no route back to. Remedy: register with a home (`mobile register`), or ask on the team"
                      " plane with `-t` if the target is a teammate."));
        return;
    }
    if (r.code == meshroute::CmdCode::err_tx_ring_full) {
        // ★ §id-hash S1c: the ONLY transient refusal on this verb — say so, or the operator treats it like the others
        // and starts changing configuration that was never wrong.
        out.println(F("> reqpubkey: nothing aired — the radio's deferred-TX ring is full (the channel has been busy)."
                      " TRANSIENT: just retry in a moment. Nothing is misconfigured."));
        return;
    }
    if (r.code == meshroute::CmdCode::err_unsupported) {
        out.println(F("> reqpubkey: nothing aired — the target is not a queryable peer (hash 0, or this node's own"
                      " key_hash32). Remedy: name another node's hash or id."));
        return;
    }
    if (r.code == meshroute::CmdCode::err_ambiguous_plane) {
        // §3-D9: BOTH planes hold this number, and they may be different peers (§18). Refusing beats guessing here
        // because the next step spends AIRTIME at a hash we picked for the operator.
        out.print(F("> reqpubkey: id ")); out.print(id);
        out.println(F(" resolves in BOTH planes (§18: a static node_id and a team local id may be the same number, and"
                      " need not be the same peer). Remedy: say which — `reqpubkey <id> -s` (static) or `-t` (team)."
                      " `hashof <id>` prints both rows with their hashes."));
        return;
    }
    if (r.code != meshroute::CmdCode::err_no_binding || id == 0) return;
    out.print(F("> reqpubkey: no id->hash binding for ")); out.print(id);
    if (r.plane == 0)      out.println(F(" in EITHER plane. A pubkey request must be addressed to a key_hash32, and no"
                                        " beacon has bound this id to one. Remedy: wait for / provoke a beacon from it,"
                                        " import its QR with `peerkey <hex64>`, or use `reqpubkey 0x<hash>` directly."
                                        " `hashof <id>` reads the same view and will agree."));
    else if (r.plane == 1) out.println(F(" on the TEAM plane (searched because of `-t`). Remedy: drop `-t` to search"
                                        " both planes, or wait for that teammate's beacon — the team key cache is fed"
                                        " ONLY by a directly-heard same-team beacon."));
    else                   out.println(F(" on the STATIC plane (searched because of `-s`). Remedy: drop `-s` to search"
                                        " both planes, or wait for that node's beacon — only an AUTHORITATIVE (first-hand"
                                        " beacon / owner-confirmed) binding qualifies."));
}

// ★★ §AB3 `peers` / `peers all` — the GENERATED address book as text (spec §2.1). The bounded form (rows backed by the
// 16-slot peer-key cache) is the address book proper; `peers all` adds the up-to-256 id-only diagnostic rows and is
// TEXT-CONSOLE ONLY per the §2.6(a) ruling — a few hundred rows over BLE is the self-inflicted console-flood wedge this
// project has already fixed once (the mrcon drop-never-block sink). U3: line shape mirrors dump_routes' `[route] k=v`.
static void peers_text_row(const meshroute::Node::PeerBookRow& r, void* ctx) {
    Print& out = *static_cast<Print*>(ctx);
    out.print(F("[peer]"));
    if (r.hash) { out.print(F(" hash=0x")); out.print(r.hash, HEX); }
    if (r.name_len) { out.print(F(" name=\"")); out.write(r.name, r.name_len); out.print('"'); }
    // ★ §id-hash S2: the (auth)/(claimed) suffix is printed ONLY when there is a hash to be authoritative ABOUT. An
    // id-only row (the new `_rt` pass 2b, and pass 2's hash==0 shape) holds NO binding at all, and labelling that
    // `static_id=48(claimed)` would report a claim nobody made — the opposite of the honesty this dump exists for.
    // A bare `static_id=48` reads correctly: we route to it, we cannot name it.
    if (r.static_id) { out.print(F(" static_id=")); out.print(r.static_id);
                       if (r.hash) out.print(r.static_authoritative ? F("(auth)") : F("(claimed)")); }
    if (r.team_id)   { out.print(F(" team_id="));   out.print(r.team_id); }
    if (r.has_key) { out.print(F(" conf=")); out.print(meshroute::console::peerkeyconf_name(r.conf));
                     out.print(F(" confirmed=")); out.print(r.peer_confirmed ? 1 : 0); }
    else if (r.hash && r.name_len) out.print(F(" key=AGED(unusable — reqpubkey to refresh)"));
    // ★★ §AB4 the retained position (spec §2.7). The AGE and the SOURCE ride with it, never the pin alone: an age-less
    // position reads as current, and a `team`-anchored one is only "some holder of the team key said so" (the shared
    // content key proves membership, not identity) where a `peer`-anchored one was sealed to us. RAM-only — a reboot
    // clears every one of these on purpose. Most rows carry no position at all, and that is normal, so nothing prints.
    if (r.has_location) {
        out.print(F(" loc=")); out.print(r.lat_e7); out.print(','); out.print(r.lon_e7);
        out.print(F(" age=")); out.print(r.loc_age_s); out.print(F("s src="));
        out.print(meshroute::console::peerlocsrc_name(r.loc_src));
    }
    if (r.team_alias_dropped) { out.print(F(" +")); out.print(r.team_alias_dropped); out.print(F(" stale team-id alias dropped")); }
    out.println();
}
static void dump_peers(Print& out, bool all) {
    const uint16_t n = g_node.peer_book_walk(all, peers_text_row, &out);
    if (!n) out.println(F("empty"));
    out.print(F("[peers] count=")); out.print(n);
    out.print(all ? F(" (all: keyed + id-only)") : F(" (keyed only — `peers all` adds id-only rows)"));
    out.println();
}

// `peers` over BLE/companion: the BOUNDED book only — one {"ev":"peer",…} per _peer_keys-backed row then
// {"ev":"peers_end","count":N}. Mirrors handle_routes exactly (U3), including the shared s_inbox_jb scratch.
static void peers_json_row(const meshroute::Node::PeerBookRow& r, void* ctx) {
    Print& out = *static_cast<Print*>(ctx);
    const size_t m = meshroute::console::write_peer_row(s_inbox_jb, sizeof s_inbox_jb, r);
    if (m) out.write(s_inbox_jb, m);
}
void handle_peers(Print& out) {
    const uint16_t n = g_node.peer_book_walk(/*include_id_rows=*/false, peers_json_row, &out);
    const size_t m = meshroute::console::write_peers_end(s_inbox_jb, sizeof s_inbox_jb, n);
    if (m) out.write(s_inbox_jb, m);
}

// `whoami` — this node's own identity + role. The hash printed here is what a peer types into `sendhash` to
// reach you (the device can't surface its own key_hash32 any other way). Name is read from /mrid.
static void handle_whoami(Print& out) {
    out.print(F("[whoami] id=")); out.print(g_node.node_id());
    out.print(F(" hash=0x"));     out.print(g_node.key_hash32(), HEX);
    { char nm[32]; uint8_t nn = g_node.effective_name(nm, sizeof nm); out.print(F(" name=\"")); out.write(nm, nn); out.print('"'); }   // §1.3: always show the effective name (the MeshRoute node: 0x<hash> default when unset)
    const meshroute::NodeConfig& c = g_node.config();
    out.print(F(" leaf="));   out.print(c.leaf_id);
    out.print(F(" gw="));     out.print(c.is_gateway ? 1 : 0);
    out.print(F(" gwonly=")); out.print(c.gateway_only ? 1 : 0);
    out.print(F(" mobile=")); out.println(c.is_mobile ? 1 : 0);
    // Dual-layer gateway: an ADDITIVE per-leaf line. Single-layer whoami above is BYTE-IDENTICAL to before.
    if (c.n_layers == 2) {
        for (uint8_t li = 0; li < 2; ++li) {
            const meshroute::LayerConfig& L = c.layers[li];
            out.print(F("[whoami.layer")); out.print(li);
            out.print(F("] node_id="));   out.print(L.node_id);
            out.print(F(" layer_id="));   out.print(L.layer_id);
            out.print(F(" routing_sf=")); out.print(L.routing_sf);
            out.print(F(" window_ms="));  out.print(L.window_ms);
            out.print(F(" window_offset_ms=")); out.println(L.window_offset_ms);
        }
    }
}

// gw_parse_err_str / gw_val_err_str / handle_gateway moved to firmware_config.{h,cpp} (cleanup 2026-07-14, Increment A); `using mrfw::handle_gateway` (top).

// ---- `leaf` command REMOVED (2026-07-03) --------------------------------------------------------------------
// The low-level `leaf create` (which minted a leaf from the node's CURRENT settings) is folded into `create`
// (explicit key=value params; anti-spam knobs default rather than inherit). `leaf name <text>` -> `cfg set
// leaf_name "<text>"` (the config-hash rename that bumps the epoch). One leaf-mint verb now: `create`.

// ---- R6.3 normal-node provisioning verbs: join / create / leave — LIVE-APPLY (no reboot). Spec
//      2026-06-21-leaf-provisioning-console-verbs.md. `create` is the ONE leaf-mint verb (2026-07-03: the old
//      low-level `leaf create` folded in; `key=value` args); `cfg set <key>` stays the granular per-field path
//      (§4). Normal nodes ONLY (gateways are multi-layer -> a future join_as_gateway, §5). ------------------

// seed_blob_from_live + provision_apply_live moved to firmware_config.{h,cpp} (cleanup 2026-07-14, Increment B) — internal (static) there.

// handle_join / handle_create / handle_team / handle_mobile moved to firmware_config.{h,cpp} (cleanup 2026-07-14, Increment B); `using mrfw::handle_*` above (guarded #if MR_N_LAYERS<2 / MR_FEAT_MOBILE).

// handle_leave moved to firmware_config.{h,cpp} (cleanup 2026-07-14, Increment B); `using mrfw::handle_leave` above.

// A DRAINED, CHUNKED console line for the multi-line dumps: emits the WHOLE line even when it doesn't fit the
// current CDC TX FIFO free space — writing only what fits, then yielding to the async USB drainer, and repeating.
// mrcon's whole-chunk write DROPS a line that doesn't fit (the `help` truncation: the two LONGEST lines were
// discarded whole while shorter ones slipped through), so the bulk dumps write here instead. Per-line budget
// (≤40 ms) => a stalled/absent host still never hangs loop(). NOT the radio hot path; bypasses mrcon deliberately.
static void hl(const __FlashStringHelper* fs) {
#if MR_CONSOLE
    if (!Serial) return;
    const char* s = reinterpret_cast<const char*>(fs);   // nRF52/ESP32: F() is memory-mapped flash — a direct read is fine (Print::println does the same)
    const size_t len = strlen(s); size_t off = 0; const uint32_t t0 = millis();
    while (off < len && Serial && (uint32_t)(millis() - t0) < 40) {
        const int a = Serial.availableForWrite();
        if (a > 0) { const size_t rem = len - off; const size_t chunk = (static_cast<size_t>(a) < rem) ? static_cast<size_t>(a) : rem;
                     off += Serial.write(reinterpret_cast<const uint8_t*>(s) + off, chunk); }
        yield();
    }
    if (Serial && Serial.availableForWrite() >= 2) Serial.write(reinterpret_cast<const uint8_t*>("\r\n"), 2);
#else
    (void)fs;
#endif
}

// `help` / `?` — the command + cfg-key reference for the live console session. Grouped with blank separators for
// readability; a category label starts each group, continuation lines indent under it. (hl() writes per-line.)
static void dump_help(Print& out) {
    hl(F("===== MeshRoute console ====="));
    hl(F(""));
    hl(F("MESSAGING"));
    hl(F("  send <id|0xhash> \"<text>\" [-a] [-e] [-t] [-l] -a=ack  -e=encrypt(hash only)  -t=team plane; plain send=global/home (fails if no home)"));
    hl(F("    -l = attach this node's position to THIS message. REFUSED unless the DM is SEALED (use -e, or `cfg set e2e_dm 1`),"));
    hl(F("         refused if this node has no fix (set lat/lon), and refused as too_large if the +6 B no longer fits."));
    hl(F("         Replaces the removed `cfg set loc_dm`, which aired coordinates in the clear."));
    hl(F("  send_channel <ch> \"<text>\" [-t] [-g] [-e] [-l]   -t=team plane  -g=explicit global  `-t -g`=BOTH  plain=global"));
    hl(F("    -e = seal the post to the TEAM content key. Valid ONLY as `-t -e`: a global channel has no key, and"));
    hl(F("         `-t -g -e` is refused because the global copy would air the same text in the clear. With a key held,"));
    hl(F("         `-t` seals by DEFAULT (`cfg set team_channel_crypt 0` opts out).  No -a on this verb."));
    hl(F("    -l = attach this node's position INSIDE the seal. `-t -l -e` sends text AND position in one sealed post."));
    hl(F("         REFUSED whenever the post would not actually be sealed (no team, no key, crypt off, or `-t -g`),"));
    hl(F("         and refused `no_location` with no fix. Max text 173 B sealed, 167 B with -l."));
    hl(F("  send_layer <0xhash> <l1,l2,…> \"<text>\" [-a] [-e]   explicit cross-layer destination path; -e=encrypt (sealed relay); -l is refused (cross-layer carries no position)"));
    hl(F(""));
    hl(F("IDENTITY / KEYS"));
    hl(F("  whoami | lookup 0x<hash> | hashof <id> [-t|-s] | nameof 0x<hash> | resolve 0x<hash> [hard]   (hashes are 0x-prefixed; hashof prints 0x…)"));
    hl(F("    hashof searches BOTH id planes (static node_id and team local id) and NAMES the one that matched;"));
    hl(F("    -t/-s narrow it. One number can legitimately answer in both — then both lines print."));
    hl(F("  peers | peers all    the address book: known peers (hash, name, static/team id, key confidence)."));
    hl(F("    plain = the 16 rows we hold a key for (also the JSON book over BLE); `all` adds id-only rows (console only)."));
    hl(F("  peerkey <ed_pub hex64> [\"<name>\"]   pin a scanned/QR pubkey (optional one-shot label, max 32)"));
    hl(F("  peername 0x<hash> \"<name>\"  rename a CACHED peer (key + confidence untouched; works on a pinned peer)"));
    hl(F("  reqpubkey <0xhash|team-id>  request a peer's key on-air (0xhash, or a bare team-id via the team cache)"));
#if MR_N_LAYERS < 2
    hl(F(""));
    hl(F("MOBILE / TEAM  (normal-node only)"));
    hl(F("  mobile register [freq=<MHz> sf=<5-12> bw=<kHz> | scan]     arm registration: current PHY / a given PHY / scan known networks"));
    hl(F("  mobile gateways            list learned gateways + networks"));
    hl(F("  mobile query <gw>          pull a gateway's network directory"));
    hl(F("  mobile status              registration + current PHY + autoregister"));
    hl(F("  team new                   mint a team (become its creator) — ALWAYS also mints its X25519 channel key"));
    hl(F("  team <id> | team 0         join an existing team / leave (a join mints NO key — receive it by grant or QR)"));
    hl(F("    [tkpub=<64 hex> tkpriv=<64 hex>]   adopt an EXISTING team channel keypair instead of minting (QR onboarding)"));
    hl(F("  team exportkey             print this team's channel keypair as JSON (the app's team QR) — \xe2\x9a\xa0 discloses a PRIVATE key"));
    hl(F("  team grantkey <0xhash|team-id> [name=\"<text>\"] [-t]   send this team's channel key to a teammate in a SEALED DM"));
    hl(F("                             needs a VERIFIED pubkey for the target (`reqpubkey <0xhash>` or a QR import) — never sent in the clear"));
#endif
    hl(F(""));
    hl(F("INBOX"));
    hl(F("  pull_inbox <dm_since> <chan_since> | mark_read <dm|chan> <seq>       NDJSON out"));
    hl(F(""));
    hl(F("DIAGNOSTICS"));
    hl(F("  routes | status | duty | limits | cfg | cfg set <k> <v>"));
    hl(F("  sleep [on|off] | debug [on|off] | regen | reboot | ota"));
    hl(F("  version            build/git/board + last reset (no reset)"));
    hl(F("  faults             the flash fault ring"));
    hl(F("  crashtest <hang|fault|reboot>      (needs `debug on`)"));
    hl(F("  rcmd <dst> <verb>      remote command via DM. status/routes = OPEN (cleartext); everything else = SEALED (needs `unlock`)"));
    hl(F("  prep-restart       clear routes+inbox, KEEP join, go DORMANT (run fleet-wide, then power-cycle)"));
#if MR_FEAT_REMOTE_MGMT
    hl(F(""));
    hl(F("REMOTE MANAGEMENT  (authenticated; static/gateway builds only)"));
    hl(F("  password <pass>        LOCAL-only: pin the fleet admin pubkey (derive from the passphrase) — set on every node"));
    hl(F("  unlock <pass> | lock   operator device: derive the admin key into RAM to sign `rcmd`s / wipe it"));
    hl(F("    then: `rcmd <dst> reboot` (etc.) is sealed to <dst>; `reqpubkey 0x<hash>` first if the target's pubkey isn't cached"));
#endif
    hl(F(""));
    hl(F("TEST"));
    hl(F("  route add <dest> <next_hop> <hops> [score_q4] | route del <dest>"));
    hl(F("  testsend <dst> <run> [-a] [-e] -t ms1,ms2,… | testch <ch> <run> -t ms1,ms2,… | teststatus | testclear"));
    hl(F("  factory_reset confirm      WIPE all flash (config+identity+peers+inbox) -> factory reboot"));
    hl(F(""));
    hl(F("PROVISIONING     (key=value, order-free; LIVE, no reboot)"));
    hl(F("  create layer= freq= bw= sf= sf_list= duty= name=\"<n>\" [active_fraction=] [ch_min_ms=] [dm_min_ms=]"));
    hl(F("  join layer= freq= bw= sf=      |  leave              layer=1..255 network id (leaf = layer & 0x0F)"));
    hl(F("  gateway l0=<layer>:<node>:<ctrl_sf>:<data_sfs> l1=…  [period=] [win0=ms:off] [win1=] [beacon=] [freq0=] [freq1=] [bw0=] [bw1=] [cr0=] [cr1=] [gateway_only=]"));
    hl(F("    dual-layer -> NV, reboot to apply.  e.g. gateway l0=1:1:8:7,9 l1=2:1:9:9,10"));
    hl(F(""));
    hl(F("CFG KEYS  (`cfg set <key> <val>`; bool keys take on|off / 1|0)"));
    hl(F("  node_id name freq routing_sf bw cr tx_power sf_list lbt beacon_ms duty nav nav_ignore hop_cap team_hop_cap leaf_id"));
    hl(F("  mobile mobile_autoregister host_mobiles intra_layer_relay gateway_only"));   // §team-id-cfg-removal: `team_id` REMOVED — a team is joined/left with the `team` verb (see below)
    hl(F("  lat lon e2e_dm team_channel_crypt intro_attach ble_mode ble_period ble_pin gw_announce_pct gw_announce_interval gw_herd_slack"));   // §loc-per-send: `loc_in_dm` REMOVED — use `send … -l` per message
    hl(F("  active_fraction ch_min_ms dm_min_ms leaf_name"));
    hl(F("    `name`=node identity · `leaf_name`=managed leaf (bumps epoch) · identity via `regen` · NO team_id key -> `team new`/`team <id>`/`team 0`"));
    hl(F("  gateway-only keys: n_layers layer0_id window_period_ms l0_window_ms l0_window_offset_ms l1_layer_id l1_node_id l1_routing_sf l1_sf_list l1_beacon_ms l1_window_ms l1_window_offset_ms l1_freq"));
}

// `limits` verb (USB): the companion anti-spam/headroom snapshot as one NDJSON line. Composed from limits_snapshot()
// then serialized via write_limits() into s_inbox_jb (declared just above) — same pattern as the other JSON dumps. A
// local-only read (no OTA change): NOT in the rcmd remote allow-list. Mirrors the BLE `limits` handler.
static void dump_limits(Print& out) {
    const auto s = g_node.limits_snapshot();
    meshroute::console::LimitsFields L;
    L.win_ms = s.win_ms; L.win_left_ms = s.win_left_ms; L.n = s.n; L.ch_sf = s.ch_sf;
    L.ch_cap = s.ch_cap; L.ch_used = s.ch_used; L.ch_min_ms = s.ch_min_ms;
    L.ch_next_ms = s.ch_next_ms; L.ch_ceiling = s.ch_ceiling;
    L.dm_min_ms = s.dm_min_ms; L.dm_next_ms = s.dm_next_ms;
    L.duty_ms = s.duty_ms; L.duty_used_ms = s.duty_used_ms;
    const size_t m = meshroute::console::write_limits(s_inbox_jb, sizeof s_inbox_jb, L);
    if (m) out.write(s_inbox_jb, m);   // JSON line to USB (mirrors the other write_* dumps)
}

// Firmware scheduled-send (spec 2026-06-24): arm the node to fire DMs/channel posts on an ms-offset schedule OVER THE
// RADIO, so the oracle touches USB only to arm + read (killing the continuous-stream USB-CDC death). `testsend <dst>
// <run> [-a] [-e] -t ms1,ms2,…` / `testch <ch> <run> -t ms1,ms2,…` — APPENDS (seq keeps counting). Offsets are ms
// from NOW (arm). The fired body = the harness tag `T<run>S<self>#<seq>` + `@<sendms>` (built in the loop tick).
static void handle_testsched(char* args, bool is_channel, Print& out) {
    char* toks[12]; int nt = 0;
    for (char* p = strtok(args, " "); p && nt < 12; p = strtok(nullptr, " ")) toks[nt++] = p;
    if (nt < 2) { out.println(F("> err usage: testsend <dst> <run> [-a] [-e] -t ms1,ms2,…  |  testch <ch> <run> -t ms1,ms2,…")); return; }
    const char* dst_s = toks[0];
    const char* run_s = toks[1];
    const char* list_s = nullptr; bool ack = false, enc = false;
    for (int i = 2; i < nt; ++i) {
        if      (!strcmp(toks[i], "-a")) ack = true;
        else if (!strcmp(toks[i], "-e")) enc = true;
        else if (!strcmp(toks[i], "-t") && i + 1 < nt) list_s = toks[i + 1];
    }
    if (!list_s) { out.println(F("> testsched err: missing -t <ms,ms,…>")); return; }
    // <run> must be ALNUM — the host reconcile regex `T([0-9A-Za-z]+)S…` only matches alnum, so a hyphen/dot/_ run
    // would send a body the harness CAN'T parse -> every message silently unreconciled. Fail loud instead.
    for (const char* q = run_s; *q; ++q)
        if (!((*q >= '0' && *q <= '9') || (*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z'))) {
            out.println(F("> testsched err: <run> must be alphanumeric (the host tag regex)")); return; }
    uint32_t target = 0, v = 0; uint8_t flags = 0;
    if (is_channel) {
        // ⚠ V1 2026-07-31: this used to read *"matches send_channel (rejects them)"* and half of that is now FALSE —
        // §chan-crypt CL1 made `send_channel` ACCEPT `-e`. The refusal here is kept DELIBERATELY (behaviour unchanged,
        // C1): the scheduled-send workload has no `-t`, so any `-e` it forwarded would be the `no_team` refusal, and
        // `mrsched` carries no plane bit to fix that with. ✖ MISSING: a sealed `testch` — TRIGGER: CL2 + a plane flag here.
        if (ack || enc) { out.println(F("> testch err: -a/-e not valid on a channel")); return; }   // -a matches send_channel (O3: no ack); -e is a testch-only limit, see above
        if (!mrsched::parse_dec(dst_s, 255, v)) { out.println(F("> testch err: channel 0..255")); return; }
        target = v; flags |= mrsched::kChannel;
    } else {
        uint32_t h;
        if (mrsched::parse_hash8(dst_s, h)) { target = h; flags |= mrsched::kHash; }
        else if (mrsched::parse_dec(dst_s, 254, v) && v >= 1) { target = v; }
        else { out.println(F("> testsend err: dst 1..254 or 8-hex hash")); return; }
        if (enc && !(flags & mrsched::kHash)) { out.println(F("> testsend err: -e (encrypt) needs an 8-hex hash dst")); return; }   // matches `send` (allow_e=by_hash)
        if (ack) flags |= mrsched::kAck;
        if (enc) flags |= mrsched::kEnc;
    }
    g_sched.set_run(run_s);
    uint32_t offs[128];
    const uint16_t no = mrsched::parse_offsets(list_s, offs, 128);
    if (no == 0) { out.println(F("> testsched err: no offsets parsed")); return; }
    if (no == 128) out.println(F("> testsched warn: offset list capped at 128 — split into more lines"));   // never silent
    const uint32_t base = (uint32_t)g_hal.now();
    uint16_t added = 0;
    for (uint16_t i = 0; i < no; ++i) if (g_sched.add(base + offs[i], target, flags) >= 0) ++added;
    out.print(F("> ")); out.print(is_channel ? F("testch") : F("testsend"));
    out.print(F(" run="));   out.print(g_sched.run);
    out.print(is_channel ? F(" ch=") : F(" dst=")); out.print(dst_s);
    out.print(F(" +"));      out.print(added);
    out.print(F(" armed=")); out.print(g_sched.armed());
    if (added < no) out.print(F(" (SCHED FULL — rest dropped)"));
    out.println();
}

static void handle_teststatus(Print& out) {
    const uint32_t mnow = (uint32_t)g_hal.now();
    const int32_t nx = g_sched.next_offset_ms(mnow);
    const char* state = (g_sched.armed() == 0) ? "idle" : (g_sched.done() ? "done" : "running");
    out.print(F("[teststatus] run=")); out.print(g_sched.run[0] ? g_sched.run : "-");
    out.print(F(" armed="));    out.print(g_sched.armed());
    out.print(F(" fired="));    out.print(g_sched.fired);
    out.print(F(" deferred=")); out.print(g_sched.deferred);
    out.print(F(" dropped="));  out.print(g_sched.dropped);
    out.print(F(" next="));     if (nx < 0) out.print('-'); else { out.print('+'); out.print(nx); }
    out.print(F(" state="));    out.println(state);
}

// handle_password moved to firmware_config.{h,cpp} (cleanup 2026-07-14, Increment B; MR_FEAT_REMOTE_MGMT-gated); `using mrfw::handle_password` above.

bool dispatch(const char* line, size_t len, Print& out) {   // §command-sink-consolidation: the single line->handler verb map (was service_debug); every response goes to `out`
    if ((len == 4 && !strncmp(line, "help", 4)) || (len == 1 && line[0] == '?')) { dump_help(out); return true; }
    if (len == 7 && !strncmp(line, "version", 7))  { print_banner(out); return true; }
    if (len == 6 && !strncmp(line, "faults", 6))   { fw_faults_dump(out);  return true; }
    if (len == 12 && !strncmp(line, "prep-restart", 12)) { fw_prep_restart(out); return true; }
    if ((len == 4 || (len > 4 && line[4] == ' ')) && !strncmp(line, "rcmd", 4)) { handle_rcmd(line + 4, out); return true; }
    if (len == 9 && !strncmp(line, "testclear", 9))    { g_sched.clear(); out.println(F("> testsched cleared")); return true; }
    if (len == 10 && !strncmp(line, "teststatus", 10)) { handle_teststatus(out); return true; }
    if ((len == 8 || (len > 8 && line[8] == ' ')) && !strncmp(line, "testsend", 8)) {   // strtok needs a mutable copy
        static char tb[512]; strncpy(tb, line + 8, sizeof tb - 1); tb[sizeof tb - 1] = '\0'; handle_testsched(tb, /*channel=*/false, out); return true; }
    if ((len == 6 || (len > 6 && line[6] == ' ')) && !strncmp(line, "testch", 6)) {
        static char tb[512]; strncpy(tb, line + 6, sizeof tb - 1); tb[sizeof tb - 1] = '\0'; handle_testsched(tb, /*channel=*/true, out);  return true; }
    if ((len == 9 || (len > 9 && line[9] == ' ')) && !strncmp(line, "crashtest", 9)) { fw_crashtest(line + 9, out); return true; }
    if (len == 6 && !strncmp(line, "routes", 6))   { dump_routes(out); return true; }
    // ★ §AB3: `peers` = the bounded address book; `peers all` = + the id-only diagnostic rows (text only, §2.6(a)).
    // C2: any other tail refuses loud rather than being read as `all`.
    if (len == 5 && !strncmp(line, "peers", 5))    { dump_peers(out, /*all=*/false); return true; }
    if (len > 5 && !strncmp(line, "peers ", 6)) {
        const char* a = line + 6; size_t an = len - 6;
        while (an && *a == ' ') { ++a; --an; }
        if (an == 3 && !strncmp(a, "all", 3)) { dump_peers(out, /*all=*/true); return true; }
        out.println(F("> peers err bad_args (usage: peers | peers all)")); return true;
    }
    if (len > 6 && !strncmp(line, "route ", 6))     { handle_route_cmd(line + 6, out); return true; }   // manual route inject/del (testing)
    if (len == 6 && !strncmp(line, "status", 6))   { dump_status(out); return true; }
    if (len == 4 && !strncmp(line, "duty", 4))     { dump_duty(out);   return true; }
    if (len == 6 && !strncmp(line, "limits", 6))   { dump_limits(out); return true; }   // companion anti-spam/headroom snapshot (local-only)
    if (len == 6 && !strncmp(line, "reboot", 6))   { fw_reboot();   return true; }
    if ((len == 13 || (len > 13 && line[13] == ' ')) && !strncmp(line, "factory_reset", 13)) { handle_factory_reset(line + 13, len - 13, out); return true; }
    if (len == 5 && !strncmp(line, "regen", 5))    { do_regen();    return true; }
    if (len == 3 && !strncmp(line, "ota", 3))      { fw_ota();      return true; }
    if (len >  8 && !strncmp(line, "gateway ", 8)) { handle_gateway(line + 8, out); return true; }
#if MR_N_LAYERS < 2
    if (len >  5 && !strncmp(line, "join ", 5))    { handle_join(line + 5, out);    return true; }   // R6.3 provisioning verbs (normal-node, live)
    if (len >  7 && !strncmp(line, "create ", 7))  { handle_create(line + 7, out);  return true; }
    if (len >  5 && !strncmp(line, "team ", 5))     { handle_team(line + 5, out);    return true; }   // §mobile 6.1: `team new` (mint) / `team <id>` (join)
#if MR_FEAT_MOBILE
    if (len >  7 && !strncmp(line, "mobile ", 7))   { handle_mobile(line + 7, out);  return true; }   // §mobile console: register/gateways/query/status
#endif
#else   // §config-integrity: create/join are normal-node provisioning -> refuse on the gateway build (mirrors how `gateway` errors on a normal build) — else `create` silently re-provisions the gateway into a managed leaf.
    if ((len > 5 && !strncmp(line, "join ", 5)) || (len > 7 && !strncmp(line, "create ", 7))) {
        out.println(F("> err gateway_build (create/join are normal-node only; use `gateway l0=<layer>:<node>:<sf>:<sfs> l1=…`)"));
        return true;
    }
#endif
    if (len == 5 && !strncmp(line, "leave", 5))    { handle_leave(out);           return true; }
    if (len >  8 && !strncmp(line, "cfg set ", 8)) { handle_cfg_set(line + 8, out); return true; }
    if (len == 3 && !strncmp(line, "cfg", 3))      { dump_cfg(out);    return true; }
    if ((len == 5 || (len > 5 && line[5] == ' ')) && !strncmp(line, "sleep", 5)) { handle_sleep(line + 5, len - 5, out); return true; }
    if ((len == 5 || (len > 5 && line[5] == ' ')) && !strncmp(line, "debug", 5)) { handle_debug(line + 5, len - 5, out); return true; }
    if (len == 6 && !strncmp(line, "whoami", 6)) { handle_whoami(out); return true; }
    if ((len == 6 || (len > 6 && line[6] == ' ')) && !strncmp(line, "lookup", 6)) { handle_lookup(line + 6, len - 6, out); return true; }
    if ((len == 6 || (len > 6 && line[6] == ' ')) && !strncmp(line, "nameof", 6)) { handle_nameof(line + 6, len - 6, out); return true; }   // §1.3 peer name by hash
    if ((len == 6 || (len > 6 && line[6] == ' ')) && !strncmp(line, "hashof", 6)) { handle_hashof(line + 6, len - 6, out); return true; }
    if ((len == 10 || (len > 10 && line[10] == ' ')) && !strncmp(line, "pull_inbox", 10)) { handle_pull_inbox(line + 10, out); return true; }
    if ((len ==  9 || (len >  9 && line[9]  == ' ')) && !strncmp(line, "mark_read",   9)) { handle_mark_read(line + 9,  out); return true; }
#if MR_FEAT_REMOTE_MGMT
    if (len > 9 && !strncmp(line, "password ", 9)) { handle_password(line + 9, out); return true; }   // §remote-mgmt: LOCAL-only admin credential set
    if (len > 7 && !strncmp(line, "unlock ", 7))   { handle_unlock(line + 7, out);   return true; }   // admin-issue: derive the admin key into RAM
    if (len == 4 && !strncmp(line, "lock", 4))     { handle_lock(out);               return true; }   // wipe the unlocked admin key
#endif
    return false;
}

// ---- Node / Network screens over BLE (companion Phase 3 — roadmap Theme D) -------------------------------
// Battery (VBAT) read — pins/divider/formula are the authoritative MeshCore XiaoNrf52Board method, using
// OUR variant.h macros (VBAT_ENABLE=D14/P0.14, PIN_VBAT=D32/P0.31/AIN7, ADC_MULTIPLIER=3.0, AREF=3.0 V):
//   mV = adc × ADC_MULTIPLIER × AREF_VOLTAGE / 4.096.
// initVariant() already holds VBAT_ENABLE LOW (divider always enabled — reading costs no extra power).
// An implausible read (USB-only / no cell / floating pin) returns -1 ⇒ the field is OMITTED, never garbage.
// Compile out with -DMR_NO_BATT (or on a board without PIN_VBAT, e.g. Heltec).
static int32_t read_batt_mv() {
#if !defined(NRF52_PLATFORM) || defined(MR_NO_BATT) || !defined(PIN_VBAT)
    return -1;   // non-nRF52 (Heltec/ESP32 uses a different ADC API), compiled out, or no VBAT divider
#else
    static bool adc_ready = false;
    if (!adc_ready) {                              // configure the ADC once (a reference switch needs settling)
        pinMode(VBAT_ENABLE, OUTPUT); digitalWrite(VBAT_ENABLE, LOW);
        pinMode(PIN_VBAT, INPUT);
        analogReadResolution(12);
        analogReference(AR_INTERNAL_3_0);
        delay(2);
        adc_ready = true;
    }
    const int adc = analogRead(PIN_VBAT);
    const int32_t mv = static_cast<int32_t>((adc * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096f);
    return (mv > 2000 && mv < 4500) ? mv : -1;     // 1S-LiPo plausible range; else omit
#endif
}

// Build the rich status snapshot from the device globals (the same data dump_status prints on USB).
meshroute::console::StatusFields make_status_fields() {
    meshroute::console::StatusFields s;
    s.uptime_ms = g_hal.now();
    s.duty_ms   = static_cast<uint32_t>(g_hal.airtime_used_ms(3600000));
    s.txq       = static_cast<uint16_t>(g_hal.txq_depth());
    s.txdrop    = static_cast<uint16_t>(g_hal.txq_drops());
    s.rx        = g_rx_count;
    s.tx        = g_iradio.tx_count();
    s.routes    = g_node.rt_count();
    s.pending   = g_node.has_pending_tx();
    s.lbt       = g_node.config().lbt_enabled;
    s.batt_mv   = read_batt_mv();
    return s;
}
const char* node_state_str() { return g_node.node_id() == 0 ? "unprovisioned" : "operating"; }

// `routes` over BLE: stream one {"ev":"route",...} per table entry then {"ev":"routes_end","count":N}.
void handle_routes(Print& out) {
    const uint64_t now = g_hal.now();
    const uint8_t n = g_node.rt_count();
    for (uint8_t i = 0; i < n; ++i) {
        const meshroute::RtEntry& e = g_node.rt_at(i);
        const meshroute::RtCandidate& c = e.candidates[0];
        meshroute::console::RouteRow r;
        r.dest = e.dest; r.next = c.next_hop; r.hops = c.hops; r.score = c.score;
        r.gw = c.is_gateway; r.leaf = c.learned_leaf;
        r.age_ms = static_cast<uint32_t>(now - c.last_seen_ms); r.cand = e.n;
        const size_t m = meshroute::console::write_route(s_inbox_jb, sizeof s_inbox_jb, r);
        if (m) out.write(s_inbox_jb, m);
    }
    const size_t m = meshroute::console::write_routes_end(s_inbox_jb, sizeof s_inbox_jb, n);
    if (m) out.write(s_inbox_jb, m);
}

// Build the cfg extras (device globals not in NodeConfig) for write_cfg.
meshroute::console::CfgExtras make_cfg_extras() {
    meshroute::console::CfgExtras x;
    x.node_id    = g_node.node_id();
    x.freq_hz    = static_cast<uint32_t>(g_freq_mhz * 1000000.0 + 0.5);
    x.tx_power   = g_tx_power;
    x.duty_x1000 = static_cast<uint32_t>(g_node.config().duty_cycle * 1000.0 + 0.5);
    x.ble_mode   = g_ble_mode == 0 ? "off" : g_ble_mode == 1 ? "on" : "periodic";
    x.ble_period = g_ble_period_min;
    x.ble_pin    = g_ble_pin;
    x.lat_e7     = g_lat_e7;
    x.lon_e7     = g_lon_e7;
    x.team_ch_key = g_node.team_channel_key_present();   // §team-ch-key (T-K1b): the JSON twin of dump_cfg's `team_ch_key=` line — BOOLEAN lock state only, never the pair
    return x;
}

}  // namespace mrfw
