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
    if (_my_mobile_reg.active &&
        (_hal.now() - _my_mobile_reg.last_heard_home_ms) < protocol::mobile_home_lost_ms) {
        (void)_hal.after(protocol::mobile_reclaim_ms, kMobileDiscoverTimerId);   // still homed -> refresh later
        return;
    }
    if (_my_mobile_reg.active) mobile_reset_registration("home_lost");           // home lost -> re-enter discovery
    _mobile_offers_n = 0;
    j_discover_in d{}; d.leaf_id = _cfg.leaf_id; d.gateway_capable = false; d.is_mobile = true; d.key_hash32 = _key_hash32;
    uint8_t buf[6]; const size_t n = pack_j_discover(d, std::span<uint8_t>(buf, sizeof buf));
    if (n) {
        MR_EMIT("mobile_discover_tx", EF_I("key", static_cast<int64_t>(_key_hash32)));
        tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
    }
    (void)_hal.after(protocol::mobile_offer_window_ms, kMobileClaimGuardTimerId);   // collect, then decide
}

// Window close: pick the strongest OFFER, CLAIM its local-id, and adopt (claim-stands). No host -> exp-backoff.
void Node::mobile_claim_guard_fire() {
    if (!_cfg.is_mobile || _my_mobile_reg.active) return;
    if (_mobile_offers_n == 0) {                                   // no host answered -> exp-backoff re-DISCOVER (B3)
        _mobile_backoff_ms = _mobile_backoff_ms
            ? std::min(2u * _mobile_backoff_ms, protocol::mobile_discover_backoff_max_ms)
            : protocol::mobile_discover_backoff_min_ms;
        (void)_hal.after(_mobile_backoff_ms, kMobileDiscoverTimerId);
        MR_EMIT("mobile_no_host", EF_I("backoff_ms", static_cast<int64_t>(_mobile_backoff_ms)));
        return;
    }
    _mobile_backoff_ms = 0;
    uint8_t best = 0;
    for (uint8_t i = 1; i < _mobile_offers_n; ++i)
        if (_mobile_offers[i].snr_db > _mobile_offers[best].snr_db) best = i;
    const OfferCand o = _mobile_offers[best];
    // CLAIM the offered local-id (is_mobile) — mirrors join_start_claim's emit shape (node_join.cpp).
    j_claim_in c{}; c.leaf_id = _cfg.leaf_id; c.gateway_capable = false; c.is_mobile = true; c.key_hash32 = _key_hash32;
    c.proposed_node_id = o.proposed_local_id; c.claim_epoch = static_cast<uint8_t>(++_my_mobile_reg.epoch);
    c.chosen_host_id = o.responder_id;   // §mobile: address the CLAIM at the host we CHOSE (was a random nonce) -> only that host records us, not every flood-hearer
    uint8_t buf[11]; const size_t n = pack_j_claim(c, std::span<uint8_t>(buf, sizeof buf));
    if (n) tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
    // claim-stands: adopt now (no DENY-listen for v1 — the host recorded us on the CLAIM, Slice 2a).
    const uint8_t old_home = _my_mobile_reg.home_id;             // §mobile 4b: capture BEFORE the overwrite (0 = first registration -> no old home)
    set_identity(o.proposed_local_id, _key_hash32);               // _node_id := the host-assigned local-id (like join_adopt)
    _joined = true;
    _my_mobile_reg = { true, o.responder_id, o.proposed_local_id, o.responder_hash, _cfg.leaf_id,
                       _my_mobile_reg.epoch, _hal.now() };
    // §mobile 4b: if we re-homed to a DIFFERENT node, tell the OLD home we moved (best-effort — no ack/retry; TTL is the
    // fallback). Sent AFTER the adopt so issue_send routes it via the NEW home (active now) -> mesh -> old home; SOURCE_HASH=M
    // lets the old home attribute it; the epoch is the NEW (post-increment) one the sender must adopt. NB: the 2b FSM resets
    // _my_mobile_reg.active on home-loss BEFORE the re-CLAIM, so we key on the CAPTURED old_home, not `.active`.
    if (old_home != 0 && old_home != o.responder_id) {
        uint8_t body[2] = { o.responder_id, static_cast<uint8_t>(_my_mobile_reg.epoch) };
        (void)enqueue_data(old_home, body, 2, DATA_FLAG_SOURCE_HASH, "mobile_breadcrumb",
                           /*app_dm=*/false, DATA_TYPE_MOBILE_BREADCRUMB, CryptIntent::off);
        MR_EMIT("mobile_breadcrumb_tx", EF_I("old_home", old_home), EF_I("new_home", o.responder_id));
    }
    MR_EMIT("mobile_adopted", EF_I("home", o.responder_id), EF_I("local_id", o.proposed_local_id),
            EF_I("epoch", _my_mobile_reg.epoch));
    schedule_triggered_beacon();                                  // announce the adopted id (peers re-bind on it)
    (void)_hal.after(protocol::mobile_reclaim_ms, kMobileDiscoverTimerId);   // periodic re-CLAIM (self-heal + refresh)
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

}  // namespace MESHROUTE_NS
