// MeshRoute — src/firmware_remote.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The remote-management cluster (see firmware_remote.h) moved VERBATIM from fw_main.cpp (cleanup 2026-07-14).
// Always compiled (handle_rcmd's cleartext path is present on every profile); the SEALED admin machinery is
// MR_FEAT_REMOTE_MGMT-gated internally, with an inert remote_exec stub when off. Shared state (g_node/g_identity/
// g_admin_*/g_remote_action/loop_stack_free_bytes/…) comes from fw_context.h; REMOTE_FLAG_SEALED from the header.
#include "firmware_remote.h"
#include "fw_context.h"        // g_node, g_identity, g_hal, g_iradio, g_admin_*, g_remote_action*, g_rx_count, g_sleep_count, g_last_reset*, g_halted, loop_stack_free_bytes()
#include "admin_auth.h"        // admin_cmd_seal/verify, AdminCmd/AdminVerdict, admin_key_from_password
#include "console_binary.h"    // bin::enc_status/routes/duty/limits, bin::StatusDiag
#include "console_json.h"      // meshroute::console::StatusFields / RouteRow / LimitsFields
#include "device_nv.h"         // mrnv::Blob / load / save
#include "device_rng.h"        // mrrng::fill
// NOT device_fault.h — it defines the ISR/exception vectors (non-inline, single-TU). We only need the WDT kick,
// so fw_main exposes it as fw_wdt_feed() via fw_context.h (mirrors loop_stack_free_bytes).
#include <cstring>
#include <cstdio>

namespace mrfw {

#if MR_FEAT_REMOTE_MGMT
// Encode a data verb -> binary TLV (console_binary). Returns len, 0 if not handled / overflow. Open: status, routes.
static size_t remote_encode(const char* v, uint8_t* tlv, size_t cap) {
    using namespace meshroute::console;
    if (!strcmp(v, "status")) {
        StatusFields s{}; bin::StatusDiag d{};
        s.uptime_ms = g_hal.now(); s.rx = g_rx_count; s.tx = g_iradio.tx_count();
        s.txq = (uint16_t)g_hal.txq_depth(); s.txdrop = (uint16_t)g_hal.txq_drops();
        s.routes = g_node.rt_count(); s.pending = g_node.has_pending_tx(); s.lbt = g_node.config().lbt_enabled;
        s.duty_ms = (uint32_t)g_hal.airtime_used_ms(3600000); s.batt_mv = -1;
        d.txto = (uint32_t)g_hal.tx_timeouts(); d.rxbad = (uint32_t)g_iradio.rxbad_count();
        d.isr = (uint32_t)g_iradio.isr_count(); d.rxarm = (uint32_t)g_iradio.rx_arm_failures();
        d.slept = (uint32_t)g_sleep_count; d.stackhw = (uint16_t)loop_stack_free_bytes();
        d.reset_cause = g_last_reset_valid ? (uint8_t)g_last_reset.cause : 0; d.halted = g_halted ? 1 : 0;
        d.nf_dbm = (int8_t)g_iradio.noise_floor();
        return bin::enc_status(tlv, cap, g_node.node_id(), g_node.key_hash32(), s, d);
    }
    if (!strcmp(v, "routes")) {
        RouteRow rows[32]; uint8_t nr = 0; const uint64_t now = g_hal.now(); const uint8_t rc = g_node.rt_count();
        for (uint8_t i = 0; i < rc && nr < 32; ++i) {
            const meshroute::RtEntry& e = g_node.rt_at(i); const meshroute::RtCandidate& c = e.candidates[0];
            rows[nr++] = { e.dest, c.next_hop, c.hops, c.score, c.is_gateway, c.learned_leaf, (uint32_t)(now - c.last_seen_ms), e.n };
        }
        uint8_t tr = 0; return bin::enc_routes(tlv, cap, rows, nr, &tr);
    }
    if (!strcmp(v, "duty")) {
        const auto ds = g_node.duty_status(); return bin::enc_duty(tlv, cap, ds.pct, (uint32_t)ds.avail_ms, ds.enabled);
    }
    if (!strcmp(v, "limits")) {
        const auto ss = g_node.limits_snapshot(); LimitsFields L;
        L.win_ms=ss.win_ms; L.win_left_ms=ss.win_left_ms; L.n=ss.n; L.ch_sf=ss.ch_sf; L.ch_cap=ss.ch_cap;
        L.ch_used=ss.ch_used; L.ch_min_ms=ss.ch_min_ms; L.ch_next_ms=ss.ch_next_ms; L.ch_ceiling=ss.ch_ceiling;
        L.dm_min_ms=ss.dm_min_ms; L.dm_next_ms=ss.dm_next_ms; L.duty_ms=ss.duty_ms; L.duty_used_ms=ss.duty_used_ms;
        return bin::enc_limits(tlv, cap, L);
    }
    return 0;   // cfg / faults / gateway: same enc_* pattern (make_cfg_extras etc.) — add here as needed. version/uptime stay out (string/trivial).
}
static bool remote_verb_open(const char* v) { return !strcmp(v, "status") || !strcmp(v, "routes"); }   // spec §4

// Seal a response blob back to the admin (node -> admin; admin_cmd_seal is symmetric). Prefix REMOTE_FLAG_SEALED.
static size_t remote_seal_resp(uint8_t* out, size_t cap, const uint8_t* body, uint8_t body_len) {
    const uint8_t* apk = g_node.admin_pubkey(); if (!apk || cap < 1) return 0;
    out[0] = REMOTE_FLAG_SEALED;
    uint8_t rand8[8]; mrrng::fill(rand8, sizeof rand8);
    static uint16_t s_resp_ctr = 0;
    const uint32_t ah = (uint32_t)apk[0] | ((uint32_t)apk[1]<<8) | ((uint32_t)apk[2]<<16) | ((uint32_t)apk[3]<<24);
    const size_t sn = meshroute::admin_cmd_seal(out + 1, cap - 1, g_identity, apk, ah, 0, body, body_len, rand8, ++s_resp_ctr);
    return sn ? sn + 1 : 0;
}

void remote_exec(uint8_t from, const uint8_t* query, uint8_t qlen) {
    char v[64]; uint8_t vn = 0; bool authed = false; uint8_t pt[64];
    if (qlen >= 1 && query[0] == REMOTE_FLAG_SEALED) {                  // ---- sealed admin command ----
        if (!g_node.admin_provisioned()) return;                        // no admin pinned -> silent drop
        meshroute::AdminCmd ac{};
        const meshroute::AdminVerdict verdict = meshroute::admin_cmd_verify(
            query + 1, qlen - 1, g_node.admin_pubkey(), g_identity, g_node.admin_counter_floor(), ac, pt, sizeof pt);
        if (verdict == meshroute::AdminVerdict::replay) {               // valid open, stale counter -> the reject-hint
            char hint[32]; int hn = snprintf(hint, sizeof hint, "floor=%lu", (unsigned long)g_node.admin_counter_floor());
            uint8_t sr[64]; size_t sl = remote_seal_resp(sr, sizeof sr, (const uint8_t*)hint, (uint8_t)(hn > 0 ? hn : 0));
            if (sl) g_node.send_remote_response(from, sr, (uint8_t)sl);
            return;
        }
        if (verdict != meshroute::AdminVerdict::ok) return;             // bad_tag / wrong_node -> silent drop (no oracle)
        if (g_node.admin_counter_check_advance(ac.counter)) {           // advance + persist the replay floor
            mrnv::Blob b{}; if (mrnv::load(b)) { b.admin_counter_floor = g_node.admin_counter_floor(); mrnv::save(b); }
        }
        vn = ac.cmd_len < sizeof v - 1 ? ac.cmd_len : (uint8_t)(sizeof v - 1);
        memcpy(v, ac.cmd, vn); v[vn] = '\0'; authed = true;
    } else {                                                            // ---- cleartext -> OPEN reads only ----
        vn = qlen < sizeof v - 1 ? qlen : (uint8_t)(sizeof v - 1);
        memcpy(v, query, vn);
        while (vn && (v[vn-1]==' '||v[vn-1]=='\r'||v[vn-1]=='\n')) --vn;
        v[vn] = '\0';
        if (!remote_verb_open(v)) return;                               // a gated verb unsealed -> silent drop
    }

    if (authed && !strncmp(v, "password rotate ", 16)) {                // gated: rotate the pinned admin pubkey (sealed by the OLD admin)
        auto nib = [](char c) -> int { if (c>='0'&&c<='9') return c-'0'; if (c>='a'&&c<='f') return c-'a'+10; if (c>='A'&&c<='F') return c-'A'+10; return -1; };
        uint8_t newpk[32]; bool okhex = strlen(v + 16) >= 64;
        for (int i = 0; i < 32 && okhex; ++i) { int hi = nib(v[16 + 2*i]), lo = nib(v[16 + 2*i + 1]);
            if (hi < 0 || lo < 0) okhex = false; else newpk[i] = (uint8_t)((hi << 4) | lo); }
        if (okhex) {
            const char* ok = "ok rotate";                              // seal the ack with the OLD key FIRST (the issuing admin can open it)
            uint8_t sr[64]; size_t sl = remote_seal_resp(sr, sizeof sr, (const uint8_t*)ok, (uint8_t)strlen(ok));
            if (sl) g_node.send_remote_response(from, sr, (uint8_t)sl);
            g_node.admin_set_pubkey(newpk);                            // THEN pin the new key + reset the replay floor + persist
            mrnv::Blob b{}; if (mrnv::load(b)) { for (int i = 0; i < 32; ++i) b.admin_pubkey[i] = newpk[i]; b.admin_provisioned = 1; b.admin_counter_floor = 0; mrnv::save(b); }
        }
        return;
    }
    if (!strcmp(v, "reboot") || !strcmp(v, "prep-restart")) {           // action (gated): respond FIRST, then defer
        if (!authed) return;
        const char* ok = !strcmp(v, "reboot") ? "ok reboot" : "ok prep-restart";
        uint8_t sr[64]; size_t sl = remote_seal_resp(sr, sizeof sr, (const uint8_t*)ok, (uint8_t)strlen(ok));
        if (sl) g_node.send_remote_response(from, sr, (uint8_t)sl);
        g_remote_action = !strcmp(v, "reboot") ? 1 : 2; g_remote_action_at = g_hal.now() + 3000;
        return;
    }

    uint8_t tlv[241]; size_t tn = remote_encode(v, tlv, sizeof tlv);    // data verb -> binary TLV
    if (tn == 0) return;                                               // unknown / overflow -> silent drop
    if (authed) { uint8_t sr[241]; size_t sl = remote_seal_resp(sr, sizeof sr, tlv, (uint8_t)tn);
                  if (sl) g_node.send_remote_response(from, sr, (uint8_t)sl); }
    else        { g_node.send_remote_response(from, tlv, (uint8_t)tn); }   // open read -> unsealed TLV
}
#else
void remote_exec(uint8_t, const uint8_t*, uint8_t) {}   // §featuresplit: remote-mgmt off (mobile) -> inert
#endif

#if MR_FEAT_REMOTE_MGMT
void handle_unlock(const char* args, Print& out) {
    while (*args == ' ') ++args;
    size_t n = strlen(args);
    while (n && (args[n-1]=='\r'||args[n-1]=='\n'||args[n-1]==' ')) --n;
    if (n == 0) { out.println(F("> unlock err: usage `unlock <passphrase>`")); return; }
    out.println(F("> deriving admin key (a few seconds)..."));
    meshroute::admin_key_from_password(args, n, g_admin_id, []{ fw_wdt_feed(); });
    g_admin_unlocked = true;
    if (g_admin_tx_ctr == 0) g_admin_tx_ctr = (uint32_t)(g_hal.now() / 1000);   // seed above a likely floor; the reject-hint corrects it
    out.print(F("> admin unlocked (fp "));
    for (int i = 0; i < 4; ++i) { char hx[3]; snprintf(hx, sizeof hx, "%02X", g_admin_id.ed_pub[i]); out.print(hx); }
    out.println(F(") — `lock` to wipe"));
}
void handle_lock(Print& out) { memset(&g_admin_id, 0, sizeof g_admin_id); g_admin_unlocked = false; out.println(F("> admin locked")); }
static bool admin_verb_gated(const char* v) { return strcmp(v, "status") != 0 && strcmp(v, "routes") != 0; }   // spec §4: only status/routes are open
#endif

// Origin: `rcmd <dst> <verb>` — DM a remote command to a node (incl. multi-hop). status/routes ride cleartext (open);
// every other verb is SEALED to the target's pinned admin key (needs `unlock <pw>` + the target's ed_pub cached).
void handle_rcmd(const char* args, Print& out) {
    while (*args == ' ') ++args;
    char* end; const long dst = strtol(args, &end, 10);
    if (end == args || dst < 1 || dst > 254) { out.println(F("> rcmd err usage: rcmd <dst 1..254> <verb>  (status/routes open; others need `unlock`)")); return; }
    while (*end == ' ') ++end;
    uint8_t qn = (uint8_t)strlen(end);
    while (qn && (end[qn - 1] == '\r' || end[qn - 1] == '\n' || end[qn - 1] == ' ')) --qn;
    if (qn == 0) { out.println(F("> rcmd err: empty query")); return; }
    char verb[64]; uint8_t vl = qn < sizeof verb - 1 ? qn : (uint8_t)(sizeof verb - 1);
    memcpy(verb, end, vl); verb[vl] = '\0';
#if MR_FEAT_REMOTE_MGMT
    if (admin_verb_gated(verb)) {                                      // seal it to the target
        if (!g_admin_unlocked) { out.println(F("> rcmd err: gated verb needs `unlock <pw>` first")); return; }
        const uint32_t th = g_node.key_hash_for_id((uint8_t)dst);
        if (!th) { out.println(F("> rcmd err: unknown id (no beacon heard from it yet)")); return; }
        uint8_t tpk[32];
        if (!g_node.peer_key_find(th, tpk)) { out.print(F("> rcmd err: no pubkey for ")); out.print(dst); out.println(F(" — `reqpubkey 0x<hash>` first")); return; }
        uint8_t frame[241]; frame[0] = REMOTE_FLAG_SEALED;
        uint8_t rand8[8]; mrrng::fill(rand8, sizeof rand8);
        static uint16_t nonce_ctr = 0;
        const uint32_t tctr = ++g_admin_tx_ctr;
        const size_t fl = meshroute::admin_cmd_seal(frame + 1, sizeof frame - 1, g_admin_id, tpk, th, tctr, (const uint8_t*)verb, vl, rand8, ++nonce_ctr);
        if (!fl) { out.println(F("> rcmd err: seal overflow")); return; }
        const uint16_t c = g_node.send_remote_cmd((uint8_t)dst, frame, (uint8_t)(fl + 1));
        out.print(F("> rcmd(sealed) -> ")); out.print(dst); out.print(F(" \"")); out.print(verb);
        out.print(F("\" ctr=")); out.print(tctr); out.print(F(" dm=")); out.println(c);
        return;
    }
#endif
    if (qn > meshroute::protocol::inbox_max_body) qn = meshroute::protocol::inbox_max_body;   // open read -> cleartext
    const uint16_t ctr = g_node.send_remote_cmd((uint8_t)dst, (const uint8_t*)end, qn);
    out.print(F("> rcmd -> ")); out.print(dst); out.print(F(" \"")); out.write((const uint8_t*)end, qn);
    out.print(F("\" ctr=")); out.println(ctr);
}

}  // namespace mrfw
