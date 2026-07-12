// MeshRoute — lib/core/node_mobile.cpp  (mobile-side registration FSM — Slice 2b)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The MOBILE half of mobile-node v1: DISCOVER a host on the configured PHY, collect OFFERs for a window,
// CLAIM the strongest host's offered LOCAL id, and adopt it (set_identity, like join_adopt). Claim-stands
// (no positive confirm — the host recorded us on the CLAIM, Slice 2a); a lost CLAIM self-heals via the
// periodic re-CLAIM. DORMANT unless _cfg.is_mobile — every entry hard-guards on it, so the static mesh is
// untouched (s18 byte-identical). Origin-stamp + outbound delivery = Slice 3.
// Design: docs/superpowers/specs/2026-07-07-mobile-node-handling-assumptions.md §13/§17.

#include "node.h"
#include "frame_codec.h"

#include <algorithm>
#include <span>

namespace MESHROUTE_NS {

// DISCOVER on our PHY + open the collect-OFFERs window. Also the periodic-refresh tick: if still homed
// (a recent BCN from home), just re-arm the refresh; else (home lost / never registered) re-enter discovery.
void Node::mobile_discover_fire() {
    if (!_cfg.is_mobile) return;                                   // hard guard — a static node never enters
    // §mobile 6.4: bring the TEAM plane up on the first FSM tick, independent of the static registration outcome (and of
    // mobile_autoregister — a team member still team-DADs). A persisted/confirmed _team_local_id -> no-op (guarded).
    if (_cfg.team_id != 0 && _team_local_id == 0 && !_team_dad_pending) team_dad_fire();
    // Home-lost threshold: NEVER declare the home lost faster than ~2 of ITS beacon periods — else a slow-beaconing home
    // (e.g. beacon_ms=900000 / 15 min) trips the 90 s default every reclaim and the mobile flaps registration (drops ->
    // stamps origin=_node_id, an unroutable local id -> the reverse-ack storms). last_heard_home_ms is refreshed by any
    // frame FROM the home (its beacon + the CTS/RTS the mobile hears routing through it), so this only matters when the
    // mobile is truly silent AND the home beacons slowly.
    const uint32_t two_beacons  = 2u * _cfg.beacon_period_ms;
    const uint32_t home_lost_ms = two_beacons > protocol::mobile_home_lost_ms ? two_beacons : protocol::mobile_home_lost_ms;
    if (_my_mobile_reg.active &&
        (_hal.now() - _my_mobile_reg.last_heard_home_ms) < home_lost_ms) {
        if (_cfg.mobile_autoregister) (void)_hal.after(protocol::mobile_reclaim_ms, kMobileDiscoverTimerId);   // §console: still homed -> refresh later (autonomy)
        return;
    }
    if (_my_mobile_reg.active) mobile_reset_registration("home_lost");           // home lost -> re-enter discovery
    _mobile_offers_n = 0;
    // §mobile 5a: retune to the CURRENT scan-set PHY, then DISCOVER on ITS control SF. Only when >1 candidate — a
    // single-entry scan-set stays on the mobile's own PHY (phy == layers[0], phy.routing_sf == _cfg.routing_sf) = 2b.
    const LayerConfig& phy = scan_phy(_mobile_scan_idx);
    if (scan_set_count() > 1) {
        _hal.set_rx_sf(phy.routing_sf);
        if (phy.freq_mhz > 0.0) _hal.set_rx_freq(phy.freq_mhz);
        _hal.set_rx_bw(phy.bw_hz ? phy.bw_hz : _cfg.radio_bw_hz);
        _hal.set_rx_cr(phy.cr ? phy.cr : _cfg.radio_cr);
    }
    j_discover_in d{}; d.leaf_id = _cfg.leaf_id; d.gateway_capable = false; d.is_mobile = true; d.key_hash32 = _key_hash32;
    uint8_t buf[6]; const size_t n = pack_j_discover(d, std::span<uint8_t>(buf, sizeof buf));
    if (n) {
        MR_EMIT("mobile_discover_tx", EF_I("key", static_cast<int64_t>(_key_hash32)));
        tx_initiating(buf, n, static_cast<int16_t>(phy.routing_sf), LbtKind::flood, 0);
    }
    (void)_hal.after(protocol::mobile_offer_window_ms, kMobileClaimGuardTimerId);   // collect, then decide
}

// Window close: pick the strongest OFFER, CLAIM its local-id, and adopt (claim-stands). No host -> exp-backoff.
void Node::mobile_claim_guard_fire() {
    if (!_cfg.is_mobile || _my_mobile_reg.active) return;
    if (_mobile_offers_n == 0) {                                   // no host on THIS PHY -> §mobile 5a: advance the scan-set; exp-backoff only after a FULL cycle
        _mobile_scan_idx = static_cast<uint8_t>((_mobile_scan_idx + 1) % scan_set_count());
        uint32_t delay;
        if (_mobile_scan_idx == 0) {                               // full cycle (or single-entry) with no host anywhere -> exp-backoff (B3)
            _mobile_backoff_ms = _mobile_backoff_ms
                ? std::min(2u * _mobile_backoff_ms, protocol::mobile_discover_backoff_max_ms)
                : protocol::mobile_discover_backoff_min_ms;
            delay = _mobile_backoff_ms;
        } else {                                                   // mid-cycle -> a short inter-PHY gap so the scan sweeps promptly
            delay = protocol::mobile_offer_window_ms;
        }
        if (_cfg.mobile_autoregister) (void)_hal.after(delay, kMobileDiscoverTimerId);   // §console: backoff retry-DISCOVER (autonomy)
        MR_EMIT("mobile_no_host", EF_I("backoff_ms", static_cast<int64_t>(delay)));
        // §mobile 6.4: no static host -> ensure the TEAM plane comes up regardless (a team member self-DADs a _team_local_id
        // so an off-grid team routes among itself). Independent of the static registration; fires once (guarded on !pending && ==0).
        if (_cfg.team_id != 0 && _team_local_id == 0 && !_team_dad_pending) team_dad_fire();
        return;
    }
    _mobile_backoff_ms = 0;
    uint8_t best = 0;
    for (uint8_t i = 1; i < _mobile_offers_n; ++i)
        if (_mobile_offers[i].snr_db > _mobile_offers[best].snr_db) best = i;
    const OfferCand o = _mobile_offers[best];
    // CLAIM the offered local-id (is_mobile) — mirrors join_start_claim's emit shape (node_join.cpp).
    j_claim_in c{}; c.leaf_id = o.leaf_id; c.gateway_capable = false; c.is_mobile = true; c.key_hash32 = _key_hash32;   // §mobile: CLAIM on the CHOSEN HOST's leaf (o.leaf_id from the OFFER), NOT our own pre-adopt leaf — else the leaf-4 home drops our leaf-0 CLAIM as "foreign layer" (node_join.cpp:210, CLAIM not leaf-exempt) and never records us
    c.proposed_node_id = o.proposed_local_id; c.claim_epoch = static_cast<uint8_t>(++_my_mobile_reg.epoch);
    c.chosen_host_id = o.responder_id;   // §mobile: address the CLAIM at the host we CHOSE (was a random nonce) -> only that host records us, not every flood-hearer
    uint8_t buf[11]; const size_t n = pack_j_claim(c, std::span<uint8_t>(buf, sizeof buf));
    if (n) tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
    // claim-stands: adopt now (no DENY-listen for v1 — the host recorded us on the CLAIM, Slice 2a).
    const uint8_t old_home = _my_mobile_reg.home_id;             // §mobile 4b: capture BEFORE the overwrite (0 = first registration -> no old home)
    LayerConfig phy = scan_phy(_mobile_scan_idx);               // BY VALUE (mutated below) — freq/bw/routing_sf from the scanned PHY (already tuned here)
    phy.layer_id          = o.leaf_id;                          // §mobile: adopt the HOST's leaf (from the OFFER), NOT our own (scan_phy(0) = self)
    phy.allowed_sf_bitmap = o.data_sf_bitmap;                   // §mobile: adopt the HOST's sf_list (so last-mile DATA-SF negotiation works)
    set_identity(o.proposed_local_id, _key_hash32);               // _node_id := the host-assigned local-id (like join_adopt)
    _joined = true;
    _my_mobile_reg = { true, o.responder_id, o.proposed_local_id, o.responder_hash,
                       o.leaf_id,                                // §mobile: the HOST's leaf (from the OFFER; was phy.layer_id = self on single-PHY)
                       _my_mobile_reg.epoch, _hal.now() };
    adopt_mobile_phy(phy, /*retune_radio=*/scan_set_count() > 1);   // §mobile: config (leaf+sf_list) ALWAYS; radio retune only for a multi-PHY scan (single-PHY already tuned)
    // §mobile 4b: if we re-homed to a DIFFERENT node, tell the OLD home we moved (best-effort — no ack/retry; TTL is the
    // fallback). Sent AFTER the adopt so issue_send routes it via the NEW home (active now) -> mesh -> old home; SOURCE_HASH=M
    // lets the old home attribute it; the epoch is the NEW (post-increment) one the sender must adopt. NB: the 2b FSM resets
    // _my_mobile_reg.active on home-loss BEFORE the re-CLAIM, so we key on the CAPTURED old_home, not `.active`.
    if (old_home != 0 && old_home != o.responder_id) {
        uint8_t body[3] = { o.responder_id, static_cast<uint8_t>(_my_mobile_reg.epoch), _my_mobile_reg.home_leaf_id };  // §5b: +new_home_layer (so a stale OLD-layer home redirects with the RIGHT leaf)
        (void)enqueue_data(old_home, body, 3, DATA_FLAG_SOURCE_HASH, "mobile_breadcrumb",
                           /*app_dm=*/false, DATA_TYPE_MOBILE_BREADCRUMB, CryptIntent::off);
        MR_EMIT("mobile_breadcrumb_tx", EF_I("old_home", old_home), EF_I("new_home", o.responder_id));
    }
    // §mobile hash-locate Part 2 (Fix 6): push our E2E pubkey to the (new) home so it can answer WANT_PUBKEY locates on our
    // behalf (Option 1 — the home carries the key; the local id never leaves the home↔mobile link). A 1-hop DM to the home,
    // SOURCE_HASH=M so the home matches _mobile_reg[M] + caches ed_pub. Re-sent on EVERY (re-)adopt so a new home learns it.
    if (_crypto_ready)
        (void)enqueue_data(o.responder_id, _ed_pub, 32, DATA_FLAG_SOURCE_HASH, "mobile_pubkey_push",
                           /*app_dm=*/false, DATA_TYPE_MOBILE_PUBKEY_PUSH, CryptIntent::off);
    MR_EMIT("mobile_adopted", EF_I("home", o.responder_id), EF_I("local_id", o.proposed_local_id),
            EF_I("epoch", _my_mobile_reg.epoch));
    schedule_triggered_beacon();                                  // announce the adopted id (peers re-bind on it)
    if (_cfg.mobile_autoregister) {                              // §console: autonomy — periodic re-CLAIM + auto layer-pull (OFF -> the app drives)
        (void)_hal.after(protocol::mobile_reclaim_ms, kMobileDiscoverTimerId);   // periodic re-CLAIM (self-heal + refresh)
        (void)_hal.after(0, kMobileLayerQueryTimerId);           // §mobile 5a: pull the layer directory now (+ periodic refresh)
    }
}

// §mobile 6.4 — team-DAD: a team member self-assigns a persistent id on the team plane (no static host), so an
// off-grid team self-bootstraps. Reuses the static-DAD shape (candidate pick -> tentative claim beacon -> guard window),
// but team-SCOPED: "taken" = a known _team_peer / _rt_team dest (NOT the static id_bind/_rt). No wire change — the claim
// IS a normal team beacon (src=_team_local_id + type-5 TLV), which teammates already parse.
int Node::team_dad_choose_candidate_id() {
    auto id_taken = [&](uint8_t id) -> bool {
        if (is_team_peer(id)) return true;                                  // a known teammate holds it
        for (uint8_t i = 0; i < _active->_rt_team_count; ++i) if (_active->_rt_team[i].dest == id) return true;
        return id == _team_local_id;                                       // our current (so a re-pick on conflict avoids it)
    };
    uint8_t free_list[254]; uint16_t nfree = 0;
    for (int id = protocol::normal_node_id_min; id <= 254; ++id)           // 17..254 (1..16 = gateways)
        if (!id_taken(static_cast<uint8_t>(id))) free_list[nfree++] = static_cast<uint8_t>(id);
    if (nfree == 0) return -1;
    return free_list[_hal.rand_range(0, static_cast<int>(nfree))];
}
void Node::team_dad_fire() {
    if (!_cfg.is_mobile || _cfg.team_id == 0) return;
    const uint8_t old_tid = _team_local_id;
    const int cand = team_dad_choose_candidate_id();
    if (cand < 0) { MR_EMIT("team_dad_no_free_id", EF_I("team_id", static_cast<int64_t>(_cfg.team_id))); return; }   // 17..254 all taken on the team plane (huge team)
    _team_local_id = static_cast<uint8_t>(cand);
    // §6.4: OFF-GRID, the team-DAD'd id IS the node's link-layer id (node_id). With node_id==_team_local_id the whole
    // existing mobile link-layer — RTS/CTS/DATA/ACK src+match, deliver, cascade-route — carries team unicast DMs with NO
    // per-frame team-plane plumbing. Provision node_id whenever this member is OFF-GRID: node_id unset (first DAD), still
    // OUR previous team id (a conflict re-pick), OR never registered with a static host (a team SWITCH / leave-then-rejoin,
    // where the console cleared _team_local_id so old_tid is lost — !_my_mobile_reg.active is the durable off-grid signal).
    // A DUAL member (registered) keeps its host-assigned static id; only _team_local_id re-picks.
    if (_node_id == 0 || _node_id == old_tid || !_my_mobile_reg.active) set_identity(_team_local_id, _key_hash32);
    _team_dad_pending = true;
    emit_beacon("triggered");                                            // ★ announce the claim NOW (src=_team_local_id, §6.4 Fix 4). emit_beacon (not schedule_triggered_beacon) so the announce is IMMEDIATE — schedule_triggered_beacon jitters the send (and pre-Fix-a was a full no-op for a mobile), so the DAD guard could confirm before it ever announced. Called from a timer/console context (radio ready).
    (void)_hal.after(protocol::mobile_offer_window_ms, kTeamDadGuardTimerId);   // guard window (replace-by-id: a re-pick re-arms it)
    MR_EMIT("team_dad_claim", EF_I("id", _team_local_id));
}
void Node::team_dad_guard_fire() {
    if (!_team_dad_pending) return;                                        // already cleared (a re-pick re-armed a newer window, or set_team_local_id on boot-load/leave) -> nothing to confirm
    _team_dad_pending = false;                                            // no same-team conflict during the window -> CONFIRMED (a routable team peer; 6.2 runs)
    MR_EMIT("team_dad_adopted", EF_I("id", _team_local_id));
}

// Drop registration + go unprovisioned (transient) so the FSM re-DISCOVERs. Reuses reset_join_for_reprovision
// semantics (set_identity(unjoined)), mobile-gated.
void Node::mobile_reset_registration([[maybe_unused]] const char* reason) {
    if (!_cfg.is_mobile) return;
    _my_mobile_reg.active = false;
    _joined = false;
    set_identity(protocol::unjoined_node_id, _key_hash32);        // 0 = unprovisioned (transient; a re-CLAIM follows)
    MR_EMIT("mobile_reset", EF_S("reason", reason ? reason : ""));
}

// §mobile 5a: pull the neighbouring-layer directory from a gateway (a DM query; the gateway answers with its bridged
// layers). Armed while registered; re-arms at the refresh period. If no gateway is known yet -> no query, just re-arm.
void Node::mobile_layer_query_fire() {
    if (!_cfg.is_mobile || !_my_mobile_reg.active) return;
    const int gw = nearest_bridging_gateway();
    if (gw >= 0) {
        uint8_t q = 0;                                             // empty/reserved body; SOURCE_HASH=M lets the gw reply to us
        (void)enqueue_data(static_cast<uint8_t>(gw), &q, 0, DATA_FLAG_SOURCE_HASH, "mobile_layer_query",
                           /*app_dm=*/false, DATA_TYPE_MOBILE_LAYER_QUERY, CryptIntent::off);
        MR_EMIT("mobile_layer_query_tx", EF_I("gw", gw));
    }
    if (_cfg.mobile_autoregister) (void)_hal.after(protocol::mobile_layer_query_period_ms, kMobileLayerQueryTimerId);   // §console: periodic refresh (autonomy)
}

// §mobile 5a: a bridging gateway we can ROUTE to, from the learned type-4 TLV (gw_id -> dest_leaf). -1 = none known yet.
int Node::nearest_bridging_gateway() {
    for (uint8_t i = 0; i < protocol::cap_bridged_layers; ++i)
        if (_bridged_layers[i].valid && _bridged_layers[i].gw_id != 0 && _bridged_layers[i].gw_id != _node_id) {
            RtEntry* e = rt_find(_bridged_layers[i].gw_id);
            if (e && e->n > 0) return static_cast<int>(_bridged_layers[i].gw_id);
        }
    return -1;
}

// §mobile 5a: ingest a MOBILE_LAYER_ANSWER body = [count u8][ count × LayerRecord ]. Upsert by composite id
// (layer_id+freq+sf+bw); skip our own current layer; evict slot 0 when full (records are static, TTL-refreshed).
void Node::learned_layers_ingest(const uint8_t* body, size_t len) {
    if (len < 1) return;
    const uint8_t count = body[0];
    size_t off = 1;
    for (uint8_t c = 0; c < count && off < len; ++c) {
        size_t consumed = 0;
        auto rec = parse_layer_record(std::span<const uint8_t>(body + off, len - off), consumed);
        if (!rec || consumed == 0) break;
        off += consumed;
        if (rec->layer_id == active_layer_id()) continue;         // we're already on this one
        bool found = false;
        for (uint8_t i = 0; i < _learned_layers_n; ++i)
            if (_learned_layers[i].layer_id == rec->layer_id && _learned_layers[i].freq_khz == rec->freq_khz
                && _learned_layers[i].sf == rec->sf && _learned_layers[i].bw_hz == rec->bw_hz) { _learned_layers[i] = *rec; found = true; break; }
        if (!found) {
            if (_learned_layers_n < protocol::cap_learned_layers) _learned_layers[_learned_layers_n++] = *rec;
            else _learned_layers[0] = *rec;                       // full -> evict slot 0
        }
    }
    _learned_layers_ms = _hal.now();
    MR_EMIT("mobile_layers_learned", EF_I("n", _learned_layers_n));
}

}  // namespace MESHROUTE_NS
