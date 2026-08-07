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

// §featuresplit: the entire mobile-MEMBER registration FSM compiles out on a static/gateway build (MR_FEAT_MOBILE=0).
// A team member IS a mobile, so MR_FEAT_TEAM implies MR_FEAT_MOBILE — the inner `#if MR_FEAT_TEAM` blocks stay valid.
#if MR_FEAT_MOBILE

// DISCOVER on our PHY + open the collect-OFFERs window. Also the periodic-refresh tick: if still homed
// (a recent BCN from home), just re-arm the refresh; else (home lost / never registered) re-enter discovery.
void Node::mobile_discover_fire() {
    if (!_cfg.is_mobile) return;                                   // hard guard — a static node never enters
    // §mobile 6.4: bring the TEAM plane up on the first FSM tick, independent of the static registration outcome (and of
    // mobile_autoregister — a team member still team-DADs). A persisted/confirmed _team_local_id -> no-op (guarded).
#if MR_FEAT_TEAM
    if (_cfg.team_id != 0 && _team_local_id == 0 && !_team_dad_pending) team_dad_fire();   // §featuresplit: team plane only
#endif
    // §S6: DISCOVER is now a REGISTRATION-EVENT entry only (fresh / home-lost re-register / voluntary re-home) — the
    // 10-min re-CLAIM keepalive + the beacon-timeout home-lost rule are RETIRED (home-loss is detected by the probe
    // cycle, presence_probe_fire). If we're still registered when this fires, presence owns liveness -> just return
    // (presence_probe_fire calls mobile_reset_registration BEFORE arming us, so a real re-register sees active==false).
    if (_my_mobile_reg.active) return;
    // §autoregister ruling (2026-07-21): the DISCOVER/registration half is gated HERE (the one site every re-DISCOVER path
    // funnels through: boot-kick, mobile_id_collision, presence home-lost/epoch-mismatch/roster-absent, voluntary re-home).
    // autoregister=false + no manual `mobile register` arm => return NOW, no DISCOVER. The team-DAD above already ran, so a
    // team member still self-bootstraps + defends its team id and stays team-reachable (F-PS-1) — it just goes off-grid-QUIET
    // on the static/host plane. The manual arm is a ONE-SHOT: consume it so a later autonomous kick can't ride it.
    if (!registration_armed()) return;
    _mobile_arm_once = false;
    _mobile_offers_n = 0;
    // §MH-S1b §6.3: a new DISCOVER supersedes any CLAIM still waiting in the LBT defer ring — its staged
    // candidate lives in `_mobile_offers[0]`, which the line above has just invalidated. Clearing the flag
    // makes `mobile_claim_adopt` a NO-OP if that stale slot ever fires, instead of adopting a clobbered
    // OFFER. (Byte-inert on every non-deferring path: the flag is already false there.)
    _mobile_claim_pending = false;
    // ★★★ [[B142]] 2026-08-07 — AND THIS IS WHERE THE NEW ATTACHMENT TRANSACTION BEGINS. The three statements
    // above invalidate everything the previous transaction staged; this bump is what makes that invalidation
    // OBSERVABLE TO A COMPLETION THAT HAS ALREADY BEEN SCHEDULED. Clearing the bool was not enough: it has no
    // attempt identity, so a stale deferred CLAIM could consume the NEW transaction's stage (accepted) or
    // destroy it (rejected). The token rides `tx_initiating` -> `DeferredLbt::completion_gen`; `lbt_complete`
    // cancels any completion whose token no longer matches. ⚠ The CLAIM does NOT bump — it inherits, because a
    // DISCOVER and the CLAIM it leads to are ONE transaction (node.h's `completion_gen` contract).
    // ⓘ Bumped here rather than at the pack/send below deliberately: the P2-1 PHY-mismatch arm returns without
    //   sending, and a transaction that never puts a frame on the air must still have superseded the old one.
    ++_mobile_attach_gen;
    // §mobile 5a: retune to the CURRENT scan-set PHY, then DISCOVER on ITS control SF. Only when >1 candidate — a
    // single-entry scan-set stays on the mobile's own PHY (phy == layers[0], phy.routing_sf == _cfg.routing_sf) = 2b.
    const LayerConfig& phy = scan_phy(_mobile_scan_idx);
#if MR_FEAT_TEAM
    // §P2-1 Level 2 (ruled option (a)): a TEAM member REFUSES to DISCOVER on a scan candidate whose PHY differs from its
    // team-provisioned layers[0] (freq/bw/routing_sf/cr) — registering there would land the member on an ISOLATED island off
    // the team's shared PHY, unreachable by teammates. scan_phy(0)==layers[0] (always PHY-matches), so this ONLY ever skips a
    // LEARNED cross-PHY layer (idx>0); a cross-LAYER SAME-PHY re-home stays allowed (that is exactly the mixed-leaf case). The
    // member stays off-grid-but-team-reachable (team-DAD already fired above). Rate-limited by the scan cadence (one emit per
    // candidate per cycle). A non-team mobile (team_id==0) -> team_phy_ok()==true -> byte-identical (adopts any PHY).
    if (!team_phy_ok(phy)) {
        MR_EMIT("mobile_home_phy_mismatch", EF_I("scan_idx", _mobile_scan_idx), EF_I("layer", phy.layer_id),
                EF_I("bw_hz", static_cast<int64_t>(phy.bw_hz ? phy.bw_hz : _cfg.radio_bw_hz)), EF_I("routing_sf", phy.routing_sf));
        // §3-A.1: reach the app on metal (MR_EMIT is stripped there) — the P2-1 fail-loud refusal is otherwise invisible.
        // Rate-limited on the shared join_refused window (one push / join_refused_retry_ms) so a per-scan-cycle canvass can't spam.
        const uint64_t now = _hal.now();
        if (_last_join_refused_ms == 0 || now - _last_join_refused_ms >= protocol::join_refused_retry_ms) {
            _last_join_refused_ms = now;
            Push pu{}; pu.kind = PushKind::join_refused; pu.join_reason = JoinRefuseReason::phy_mismatch;
            pu.layer_id = phy.layer_id; pu.dst = phy.routing_sf; enqueue_push(pu);
        }
        _mobile_scan_idx = static_cast<uint8_t>((_mobile_scan_idx + 1) % scan_set_count());   // skip to the next candidate
        if (_cfg.mobile_autoregister) (void)_hal.after(protocol::mobile_offer_window_ms, kMobileDiscoverTimerId);
        return;                                                    // never DISCOVER on a mismatched PHY
    }
#endif
    if (scan_set_count() > 1) {
        _hal.set_rx_sf(phy.routing_sf);
        // §layer-freq (2026-07-27) — NOT converted here, deliberately (twin of the note in
        // Node::adopt_mobile_phy). BW/CR fall back to the global, freq does not: a scan candidate with
        // freq_mhz==0 leaves the radio on the PREVIOUS candidate's carrier instead of resetting to
        // _cfg.radio_freq_mhz. Same shape as the activate_layer bug that WAS fixed; scoped out because it is
        // the mobile plane (own scenarios, own gate), not the gateway window switch.
        if (phy.freq_mhz > 0.0) _hal.set_rx_freq(phy.freq_mhz);
        _hal.set_rx_bw(phy.bw_hz ? phy.bw_hz : _cfg.radio_bw_hz);
        _hal.set_rx_cr(phy.cr ? phy.cr : _cfg.radio_cr);
    }
    j_discover_in d{}; d.leaf_id = _cfg.leaf_id; d.gateway_capable = false; d.is_mobile = true; d.key_hash32 = _key_hash32;
    // §S6 D10: carry the last home (id/layer/epoch) so a NEW home can originate the old-home notify. 0/0/0 = fresh.
    // §B4: + the old home's HASH so the new home can address a CROSS-LAYER breadcrumb by hash (we know it from our reg).
    if (_my_mobile_reg.home_id != 0) { d.last_home_id = _my_mobile_reg.home_id; d.last_home_layer = _my_mobile_reg.home_leaf_id;
                                       d.last_reg_epoch = static_cast<uint8_t>(_my_mobile_reg.epoch); d.last_home_key_hash32 = _my_mobile_reg.home_key_hash32; }
    uint8_t buf[13]; const size_t n = pack_j_discover(d, std::span<uint8_t>(buf, sizeof buf));
    // ★★ §MH-S1 §6.1 — THE COLLECT-OFFERs WINDOW IS NO LONGER ARMED HERE.
    // It used to be armed unconditionally on the line after this call — i.e. measured from a REQUEST TO
    // SEND. A NAV/LBT defer longer than the 2 s window therefore closed the collector, reported
    // `mobile_no_host` and doubled the backoff, all before the DISCOVER had left the radio (§S0-3).
    // ⇒ the arm now lives at the handoff, in `lbt_complete`, reached by `LbtKind::mobile_discover` — on the
    //   immediate path AND when the LBT defer slot later fires. See node.h's LbtKind comment for why
    //   testing this call's `bool` instead would NOT have worked (a successful defer returns TRUE).
    // ⛔ Draw-free: no jitter is introduced here or in `mobile_admission_rejected`; S3 owns the RNG.
    if (n == 0) { mobile_admission_rejected(TxAdmission::tx_rejected, "discover_pack"); return; }   // C2: unreachable (pack_j_discover gives 13 into a 13-B span) but never silent
    MR_EMIT("mobile_discover_tx", EF_I("key", static_cast<int64_t>(_key_hash32)));
    TxAdmission adm = TxAdmission::admitted;
    // [[B142]]: the 5th argument is `completion_gen` — THIS transaction's identity, so a completion that
    // arrives after a newer DISCOVER has superseded us is cancelled instead of acted on (node.h's contract).
    if (!tx_initiating(buf, n, static_cast<int16_t>(phy.routing_sf), LbtKind::mobile_discover, _mobile_attach_gen, &adm))
        mobile_admission_rejected(adm, "discover");   // ⛔ NOT mobile_no_host: nobody was ever asked
}

// ★★ §MH-S1 §6.1/§6.3/§6.4 — one handler, both mobile-side admission sites. Contract + the reasoning for
// each of its three steps are in node.h beside the declaration; keep the two in sync if either changes.
void Node::mobile_admission_rejected(TxAdmission why, const char* site) {
    _mobile_last_result = (why == TxAdmission::defer_full) ? MobileAttemptResult::defer_full
                                                           : MobileAttemptResult::tx_rejected;
    MR_EMIT("mobile_tx_rejected", EF_S("site", site),
            EF_S("result", why == TxAdmission::defer_full ? "defer_full" : "tx_rejected"));
    // §3-A.1 twin: MR_EMIT is device-stripped, so the only way this reaches metal is a log line. Trace-gated
    // (NOT the `!!` operator-critical prefix): it is self-healing within seconds and must not spam a console.
    _hal.log("mobile attach attempt refused by OUR OWN transmitter — retrying; the home is NOT implicated");
    _mobile_arm_once = true;                                       // the attempt never happened -> don't eat a manual arm
    // Bounded retry (gate 6). ⛔ A FIXED delay, deliberately: jittering it needs a draw and S3 is the only
    // planned RNG re-anchor in this arc. Reuses the existing mid-cycle spacing constant (U1) rather than
    // introducing a second one. ⚠ Unconditional on `mobile_autoregister` — unlike the no-host backoff below
    // — because step 2 has just restored the arm, so this retry is servicing an authorised attempt that our
    // own radio dropped, not autonomous behaviour the operator switched off.
    (void)_hal.after(protocol::mobile_offer_window_ms, kMobileDiscoverTimerId);
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
        // §MH-S1 §10: THIS is the only place `no_offer` may be recorded — the window genuinely opened (the
        // DISCOVER crossed the handoff, §6.1) and genuinely nobody answered. An admission failure records
        // `tx_rejected`/`defer_full` instead and never reaches this branch at all.
        _mobile_last_result = MobileAttemptResult::no_offer;
        MR_EMIT("mobile_no_host", EF_I("backoff_ms", static_cast<int64_t>(delay)));
        // §mobile 6.4: no static host -> ensure the TEAM plane comes up regardless (a team member self-DADs a _team_local_id
        // so an off-grid team routes among itself). Independent of the static registration; fires once (guarded on !pending && ==0).
#if MR_FEAT_TEAM
        if (_cfg.team_id != 0 && _team_local_id == 0 && !_team_dad_pending) team_dad_fire();   // §featuresplit: team plane only
#endif
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
    // ★★ §MH-S1 §6.3 — A CLAIM THAT NEVER CROSSED THE ADMISSION BOUNDARY MUST NOT PRODUCE A REGISTRATION.
    // Adopting here means `set_identity`, `_joined = true`, a `mobile_reg{registered:true}` push and a
    // triggered beacon — i.e. the app is told it is registered at a home that was never sent a CLAIM.
    // Gate 6: "a rejected DISCOVER/OFFER/CLAIM is NOT reported as a successful send and has a bounded
    // retry". ★ And the classification is the point (§6.3): this is a LOCAL retry condition — it is NOT
    // evidence that the home rejected the registration, so it records `tx_rejected`/`defer_full` and
    // touches NO home-link state (gate 20).
    // ⓘ `_my_mobile_reg.epoch` was already incremented for the pack and is deliberately NOT rolled back:
    //   the retry's CLAIM then carries a strictly fresher epoch, which is what the home's claim-stands
    //   handling wants. Rolling back would re-use an epoch this node may already have put on the air.
    // ⛔ NOT the confirmed-CLAIM FSM: a CLAIM that IS admitted still claim-stands exactly as before.
    //   Confirming an ADMITTED-but-lost CLAIM against the home's roster is §7.1 and belongs to S4 (§S0-4
    //   still characterizes that defect and is deliberately left green by this slice).
    // ★★ §MH-S1b §6.3 (QA round 2) — AND THE ADOPT IS NOW ANCHORED TO THE HANDOFF, NOT TO THIS CALL.
    // Round 1 stopped a DEFINITIVELY-REFUSED CLAIM from registering but still adopted on `tx_initiating`'s
    // `true` — which is ALSO returned for a frame merely accepted into the LBT defer ring (node_mac.cpp).
    // A deferred CLAIM therefore still produced an immediate false registration, and if the HAL refused it
    // when the slot fired, `node.cpp`'s deferred-loss arm recognised only `mobile_discover` ⇒ the mobile sat
    // FALSELY REGISTERED with no CLAIM retry at all. ⇒ the chosen OFFER is STAGED here and `lbt_complete`
    // calls `mobile_claim_adopt()` at the accepted handoff (immediate or deferred), and the deferred-loss arm
    // now recognises `mobile_claim` and routes it into the same bounded local retry.
    // ⓘ The stage compacts the chosen candidate into slot 0 (U2: one carrier, no duplicate 20-byte member).
    // ★★★ [[B142]] 2026-08-07 — THE STAGE IS NOW TRANSACTION-IDENTIFIED. `_mobile_claim_pending` is kept (it
    // still answers "is there anything staged at all?") but it is NO LONGER the only correlator: the CLAIM
    // carries `_mobile_attach_gen` — INHERITED from the DISCOVER that opened this window, never bumped here —
    // so a completion that fires after a newer DISCOVER has restaged slot 0 is cancelled in `lbt_complete`
    // instead of adopting, or destroying, the newer candidate. The bool alone could not tell the two apart,
    // which is precisely what made the ABA possible.
    _mobile_offers[0] = o; _mobile_offers_n = 1; _mobile_claim_pending = true;
    TxAdmission adm = TxAdmission::admitted;
    if (n == 0 || !tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::mobile_claim, _mobile_attach_gen, &adm)) {
        _mobile_claim_pending = false;                           // definitive refusal -> nothing will adopt this CLAIM
        mobile_admission_rejected(n == 0 ? TxAdmission::tx_rejected : adm, "claim");
        return;
    }
}

// ★★ §MH-S1b §6.3 — the CLAIM crossed the accepted LBT/HAL handoff (or a deferred slot fired and did), so
// claim-stands may now run. Split VERBATIM out of `mobile_claim_guard_fire` (only the `o` binding is new:
// it reads the staged slot instead of a local), so the adopt semantics are unchanged and only its ANCHOR
// moved. Called from `lbt_complete`; see node.h for why that is the RTS discipline and not a second one.
void Node::mobile_claim_adopt() {
    if (!_mobile_claim_pending) return;                          // abandoned (a fresh DISCOVER cleared the stage) — never adopt from a clobbered slot
    _mobile_claim_pending = false;
    const OfferCand o = _mobile_offers[0];                       // the candidate `mobile_claim_guard_fire` chose and staged
    // claim-stands: adopt now (no DENY-listen for v1 — the host recorded us on the CLAIM, Slice 2a).
    const uint8_t old_home = _my_mobile_reg.home_id;             // §mobile 4b: capture BEFORE the overwrite (0 = first registration -> no old home)
    LayerConfig phy = scan_phy(_mobile_scan_idx);               // BY VALUE (mutated below) — freq/bw/routing_sf from the scanned PHY (already tuned here)
    phy.layer_id          = o.leaf_id;                          // §mobile: adopt the HOST's leaf (from the OFFER), NOT our own (scan_phy(0) = self)
    // F-SF-1 (2026-07-19): the mobile KEEPS its OWN configured sf_list (like a static — sf_list is node/leaf config, and
    // the per-exchange RTS carries only an INDEX into the agreed set). We no longer adopt the OFFER's data_sf_bitmap
    // (its `& 0xFF` pack truncated SF>=8 -> a wrong allowed-set). We PIN _cfg.allowed_sf_bitmap explicitly rather than
    // just trusting scan_phy: scan_phy(0) carries our configured set, but a CROSS-LAYER scan (idx>0) synthesizes a
    // LayerConfig whose allowed_sf_bitmap is only the LEARNED control SF (a single SF) — leaving it would replace our
    // configured set on a re-home. The OFFER byte is now advisory: a misconfig diagnostic if our configured low byte
    // disagrees with the host's offered list (an operator packed mismatched sf_lists on the leaf vs the mobile).
    phy.allowed_sf_bitmap = _cfg.allowed_sf_bitmap;             // keep our OWN configured sf_list (never adopt the host's)
    if (static_cast<uint8_t>(_cfg.allowed_sf_bitmap & 0xFF) != o.data_sf_bitmap) {
        MR_EMIT("mobile_sf_list_mismatch", EF_I("configured", static_cast<int64_t>(_cfg.allowed_sf_bitmap & 0xFF)),
                EF_I("offered", static_cast<int64_t>(o.data_sf_bitmap)));
        // §3-A.1: ADVISORY app twin (the mobile still adopts) — an operator packed disagreeing sf_lists on the mobile vs the
        // host leaf. Rate-limited on the shared join_refused window. origin=configured low byte, dst=host-offered byte.
        const uint64_t now = _hal.now();
        if (_last_join_refused_ms == 0 || now - _last_join_refused_ms >= protocol::join_refused_retry_ms) {
            _last_join_refused_ms = now;
            Push pu{}; pu.kind = PushKind::join_refused; pu.join_reason = JoinRefuseReason::sf_list_mismatch;
            pu.origin = static_cast<uint8_t>(_cfg.allowed_sf_bitmap & 0xFF); pu.dst = o.data_sf_bitmap; enqueue_push(pu);
        }
    }
    set_identity(o.proposed_local_id, _key_hash32);               // _node_id := the host-assigned local-id (like join_adopt)
    _joined = true;
    _my_mobile_reg = { true, o.responder_id, o.proposed_local_id, o.responder_hash,
                       o.leaf_id,                                // §mobile: the HOST's leaf (from the OFFER; was phy.layer_id = self on single-PHY)
                       _my_mobile_reg.epoch, _hal.now() };
    adopt_mobile_phy(phy, /*retune_radio=*/scan_set_count() > 1);   // §mobile: config (leaf+sf_list) ALWAYS; radio retune only for a multi-PHY scan (single-PHY already tuned)
    // §S6/D10: the mobile-sent breadcrumb is RETIRED — the NEW home now originates the old-home notify (it survives a
    // mobile that sleeps right after adopting, and it holds the mesh/XL route). last_home rides the j_discover +3 B block
    // (packed in mobile_discover_fire from the captured old_home). §S6 A.4: the E2E key rides the FIRST probe's HAS_PUBKEY
    // block (RETIRES the TYPE-12 push + its race); the roster's has_key bit confirms custody. (void)old_home below.
    (void)old_home;
    MR_EMIT("mobile_adopted", EF_I("home", o.responder_id), EF_I("local_id", o.proposed_local_id),
            EF_I("epoch", _my_mobile_reg.epoch));
    { Push pu{}; pu.kind = PushKind::mobile_reg; pu.origin = o.responder_id; pu.dst = o.proposed_local_id;   // §S2: registered (also a roam -> a changed home)
      pu.layer_id = _my_mobile_reg.home_leaf_id; pu.ctr = _my_mobile_reg.epoch; pu.relayed = true; enqueue_push(pu); }
    schedule_triggered_beacon();                                  // announce the adopted id (peers re-bind on it)
    presence_on_adopt();                                          // §S6: seed the presence clocks + arm the FIRST check probe (REPLACES the re-CLAIM tick)
    if (_cfg.mobile_autoregister) (void)_hal.after(0, kMobileLayerQueryTimerId);   // §S6: first-registration layer-directory pull (the PERIODIC re-arm is retired; pull now rides dir_epoch changes + the 6-h safety pull)
}

// §mobile 6.4 — team-DAD: a team member self-assigns a persistent id on the team plane (no static host), so an
// off-grid team self-bootstraps. Reuses the static-DAD shape (candidate pick -> tentative claim beacon -> guard window),
// but team-SCOPED: "taken" = a known _team_peer / _rt_team dest (NOT the static id_bind/_rt). No wire change — the claim
// IS a normal team beacon (src=_team_local_id + type-5 TLV), which teammates already parse.
#if MR_FEAT_TEAM   // §featuresplit: team-DAD compiled out on a static-only build (the header inline-stubs these)
int Node::team_dad_choose_candidate_id() {
    auto id_taken = [&](uint8_t id) -> bool {
        if (is_team_peer(id)) return true;                                  // a known teammate holds it
        for (uint8_t i = 0; i < _active->_rt_team_count; ++i) if (_active->_rt_team[i].dest == id) return true;
        return id == _team_local_id;                                       // our current (so a re-pick on conflict avoids it)
    };
    // §W2c white-box test hook: pin the FIRST team-DAD pick (deterministic hidden-terminal collision in s30). Only on
    // the FIRST DAD (_team_local_id==0); a RE-PICK (_team_local_id!=0, driven by a mediated DENY or a direct collision)
    // falls through to the random picker so the loser cannot re-pick the SAME pinned id -> convergence is preserved.
    // team_dad_pin_id defaults 0 (OFF) everywhere but s30 -> every other scenario keeps the uniform random pick.
    if (_cfg.team_dad_pin_id != 0 && _team_local_id == 0 && !id_taken(_cfg.team_dad_pin_id))
        return _cfg.team_dad_pin_id;
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
    if (cand < 0) {   // 17..254 all taken on the team plane (huge team)
        MR_EMIT("team_dad_no_free_id", EF_I("team_id", static_cast<int64_t>(_cfg.team_id)));
        // §3-A.1: mirror the static twin's join_refused{leaf_full} (node_join.cpp:179) so the app sees it on metal. Windowed.
        const uint64_t now = _hal.now();
        if (_last_join_refused_ms == 0 || now - _last_join_refused_ms >= protocol::join_refused_retry_ms) {
            _last_join_refused_ms = now;
            Push pu{}; pu.kind = PushKind::join_refused; pu.join_reason = JoinRefuseReason::leaf_full; enqueue_push(pu);
        }
        return;
    }
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
    { Push pu{}; pu.kind = PushKind::team_reg; pu.team_id = _cfg.team_id; pu.dst = _team_local_id; enqueue_push(pu); }   // §S2: team-DAD adopted / conflict re-pick
}
#endif   // MR_FEAT_TEAM

// Drop registration + go unprovisioned (transient) so the FSM re-DISCOVERs. Reuses reset_join_for_reprovision
// semantics (set_identity(unjoined)), mobile-gated.
void Node::mobile_reset_registration([[maybe_unused]] const char* reason) {
    if (!_cfg.is_mobile) return;
    const bool was_active = _my_mobile_reg.active;               // §S2: only push the deregistration on a REAL transition (no spurious repeat)
    _my_mobile_reg.active = false;
    _joined = false;
#if MR_FEAT_TEAM
    // §F-PS-1: team membership is home-INDEPENDENT (§18). A REGISTERED team member that loses its home must NOT go
    // mute on the team plane — degrade it to a normal OFF-GRID team member (the s22 steady state: node_id ==
    // _team_local_id) rather than fully unprovisioned (id 0). That keeps every `_node_id == 0` send guard correct
    // as-is (on_command :913/:930/…): a `-t` team send / team DM now originates under the intact team id instead of
    // being refused err_unprovisioned. The DISCOVER FSM keeps searching for a new home in parallel — adopt overwrites
    // _node_id with the host-assigned id, restoring the dual identity (mobile_claim_guard_fire set_identity). Mirrors
    // node_beacon.cpp:273 (team beacon src) + team_dad_fire's off-grid identity rule; a NON-team mobile / no team-DAD
    // yet (_team_local_id==0) falls through to unprovisioned = byte-identical.
    if (_cfg.team_id != 0 && _team_local_id != 0) set_identity(_team_local_id, _key_hash32);
    else
#endif
    set_identity(protocol::unjoined_node_id, _key_hash32);        // 0 = unprovisioned (transient; a re-CLAIM follows)
    MR_EMIT("mobile_reset", EF_S("reason", reason ? reason : ""));
    if (was_active) { Push pu{}; pu.kind = PushKind::mobile_reg; pu.relayed = false; enqueue_push(pu); }   // §S2: home lost / dereg -> home=0,local=0,registered:false
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
    _presence_last_pull_ms = _hal.now();
    // §S6/D6: the 10-min periodic poll is RETIRED — the directory is pulled on a dir_epoch CHANGE (presence_ingest_roster)
    // plus this slow 6-h SAFETY re-arm (catches a missed epoch bump). No dir_epoch churn ⇒ ~one pull per 6 h, not per 10 min.
    if (_cfg.mobile_autoregister) (void)_hal.after(protocol::presence_safety_pull_ms, kMobileLayerQueryTimerId);
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
        const uint64_t now = _hal.now();
        bool found = false;
        for (uint8_t i = 0; i < _learned_layers_n; ++i)
            if (_learned_layers[i].layer_id == rec->layer_id && _learned_layers[i].freq_khz == rec->freq_khz
                && _learned_layers[i].sf == rec->sf && _learned_layers[i].bw_hz == rec->bw_hz) {
                _learned_layers[i] = *rec; _learned_layers_seen_ms[i] = now; found = true; break; }
        if (!found) {
            uint8_t slot;
            if (_learned_layers_n < protocol::cap_learned_layers) slot = _learned_layers_n++;
            else {                                                // §3-A.6/P2-6: full -> evict the STALEST (min last-seen), not slot 0
                slot = 0;
                for (uint8_t i = 1; i < _learned_layers_n; ++i)
                    if (_learned_layers_seen_ms[i] < _learned_layers_seen_ms[slot]) slot = i;
            }
            _learned_layers[slot] = *rec; _learned_layers_seen_ms[slot] = now;
        }
    }
    _learned_layers_ms = _hal.now();
    MR_EMIT("mobile_layers_learned", EF_I("n", _learned_layers_n));
}

// ============================================================================
// §S6 presence plane — MOBILE side. The probe/check FSM that REPLACES the periodic re-CLAIM keepalive + layer poll.
// Home-loss is detected here in ~T + k·retry (minutes, decoupled from beacon_ms). Proactive re-home (S6.4-C) leaves
// a weak home BEFORE loss. All entries hard-guard on is_mobile -> a static build is inert (s18 byte-identical).
// ============================================================================

// Seed the presence clocks + arm the FIRST check probe. Called from the mobile adopt path (mobile_claim_guard_fire).
void Node::presence_on_adopt() {
    if (!_cfg.is_mobile) return;
    _presence_miss = 0;
    _presence_T_ms = protocol::presence_check_base_ms;
    _presence_my_tier = protocol::presence_q_ok;
    _presence_prescan = false;
    _presence_key_confirmed = false;
    _presence_reg_confirmed = false;
    _last_adopt_ms = _hal.now();
    _presence_cand_n = 0;                                          // a fresh home -> forget stale candidates
    presence_arm_check(_presence_T_ms);
}

// (Re)arm the check timer at now + delay + jitter (LBT desync).
void Node::presence_arm_check(uint32_t delay_ms) {
    if (!_cfg.is_mobile || !_cfg.mobile_autoregister) return;      // app-driven mode: the companion arms probes
    const uint32_t jitter = static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(protocol::presence_probe_jitter_ms) + 1));
    (void)_hal.after(delay_ms + jitter, kPresenceProbeTimerId);
}

// The check timer fired: send a probe (unless a fresh roster already refreshed us), else escalate toward HOME LOST.
void Node::presence_probe_fire() {
    if (!_cfg.is_mobile || !_my_mobile_reg.active) return;         // unregistered -> the DISCOVER FSM owns it
    if (_presence_miss > protocol::presence_probe_k_miss) {        // k_miss+1 unanswered probes -> HOME LOST (spec §S6.4-B)
        const uint8_t old_home = _my_mobile_reg.home_id;
        const uint8_t old_layer = _my_mobile_reg.home_leaf_id;
        const uint8_t old_epoch = static_cast<uint8_t>(_my_mobile_reg.epoch);
        MR_EMIT("presence_home_lost", EF_I("home", old_home), EF_I("miss", _presence_miss));
        mobile_reset_registration("presence_home_lost");           // -> mobile_reg{registered:false} push (S2), active=false
        // ONE SEARCHING probe (selected=0; recovery canvass — if the home was merely one-way-deaf it answers a roster and we recover w/o re-register; other homes echo as candidates)
        p_probe_in sp{}; sp.selected_home_id = 0; sp.selected_home_layer = 0; sp.key_hash32 = _key_hash32; sp.reg_epoch = old_epoch;
        sp.has_last_home = (old_home != 0); sp.last_home_id = old_home; sp.last_home_layer = old_layer;
        if (_crypto_ready) { sp.has_pubkey = true; for (int i = 0; i < 32; ++i) sp.ed_pub[i] = _ed_pub[i]; }
        uint8_t buf[42]; const size_t n = pack_p_probe(sp, std::span<uint8_t>(buf, sizeof buf));
        if (n) { MR_EMIT("presence_probe_tx", EF_I("searching", 1)); tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0); }
        (void)_hal.after(0, kMobileDiscoverTimerId);               // then the existing scan/DISCOVER machinery (re-register)
        return;
    }
    // steady `check` probe: selected = MY home (rev2 — only the selected home answers; a stale second home prunes). Attach the key until the home confirms custody (§S6 A.4).
    p_probe_in cp{}; cp.selected_home_id = _my_mobile_reg.home_id; cp.selected_home_layer = _my_mobile_reg.home_leaf_id;
    cp.key_hash32 = _key_hash32; cp.reg_epoch = static_cast<uint8_t>(_my_mobile_reg.epoch);
    if (_crypto_ready && !_presence_key_confirmed) { cp.has_pubkey = true; for (int i = 0; i < 32; ++i) cp.ed_pub[i] = _ed_pub[i]; }
    uint8_t buf[42]; const size_t n = pack_p_probe(cp, std::span<uint8_t>(buf, sizeof buf));
    if (n) { MR_EMIT("presence_probe_tx", EF_I("searching", 0)); tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0); }
    ++_presence_miss;
    presence_arm_check(protocol::presence_probe_retry_ms);         // retry spacing until a roster resets us
    presence_maybe_rehome();                                       // §S6.4-C: evaluate a proactive re-home each tick
}

// A roster heard. Our home's roster (hash+epoch match) refreshes liveness both ways + recomputes T. Any other roster is
// a candidate-home hint (leaf-free). Hash-absent / epoch-mismatch from our home -> re-register.
void Node::presence_ingest_roster(const uint8_t* frame, size_t len, const RxMeta& meta) {
    if (!_cfg.is_mobile) return;
    auto r = parse_p_roster(std::span<const uint8_t>(frame, len));
    if (!r) return;
    if (r->wire_version != protocol::wire_version) {   // §D16: a foreign-wire roster -> DROP before interpreting any field (the P-plane's own version wall)
        presence_mark_incompatible(r->home_id, r->home_layer);   // FSM B/C never DISCOVERs at this home (no wasted J rounds)
        push_join_refused_wire(r->wire_version);       // surface the mobile-flavored join_refused (rate-limited like the beacon path)
        return;
    }
    const int16_t snr_q4 = protocol::db_to_q4(meta.snr_db);
    if (_my_mobile_reg.active && r->home_id == _my_mobile_reg.home_id && r->home_layer == _my_mobile_reg.home_leaf_id) {
        int mine = -1;
        for (uint8_t i = 0; i < r->count; ++i) {
            auto e = parse_p_roster_entry(std::span<const uint8_t>(frame, len), *r, i);
            if (e && e->key_hash32 == _key_hash32) { mine = i;
                if (e->reg_epoch != static_cast<uint8_t>(_my_mobile_reg.epoch)) {           // epoch mismatch -> re-register
                    MR_EMIT("presence_epoch_mismatch", EF_I("home", r->home_id));
                    mobile_reset_registration("presence_epoch_mismatch"); (void)_hal.after(0, kMobileDiscoverTimerId); return;
                }
                // hash + epoch match -> liveness refreshed BOTH directions
                _my_mobile_reg.last_heard_home_ms = _hal.now();
                _presence_miss = 0;
                _presence_reg_confirmed = true;                                            // the home HAS us (our hash in its roster) -> a CLAIM landed
                _presence_my_tier = e->quality;                                            // D14: me->home direction (the home's report of me)
                _presence_home_rx_q4 = protocol::snr_ewma_update(_presence_home_rx_q4, snr_q4);  // D14: home->me direction (seed-if-zero EWMA of my RX of the home's rosters)
                if (e->has_key) _presence_key_confirmed = true;
                if (e->deleg_fail) {   // §B2: the home dropped a delegated send for us (loud, one-shot) -> surface send_failed{no_route} to the app's back-off
                    MR_EMIT("presence_deleg_fail", EF_I("home", r->home_id));
                    push_send_failed(SendFailReason::no_route, /*dst=*/0, /*ctr=*/0);   // dst/ctr were never set here -> the Push{} zero defaults; now EXPLICIT, value-identical
                }
                _presence_prescan = (e->quality <= protocol::presence_q_weak);
                // dynamic T (§S6.3): strong -> min(4·base,max) · ok -> base · weak/critical -> min
                _presence_T_ms = (e->quality == protocol::presence_q_strong)
                                     ? std::min(4u * protocol::presence_check_base_ms, protocol::presence_check_max_ms)
                                 : (e->quality == protocol::presence_q_ok) ? protocol::presence_check_base_ms
                                                                           : protocol::presence_check_min_ms;
                MR_EMIT("presence_roster_rx", EF_I("home", r->home_id), EF_I("tier", e->quality), EF_I("T", static_cast<int64_t>(_presence_T_ms)));
                // dir_epoch change -> jittered layer-directory pull (D6)
                if (!_presence_dir_epoch_seen || r->dir_epoch != _presence_dir_epoch) {
                    _presence_dir_epoch = r->dir_epoch; _presence_dir_epoch_seen = true;
                    if (_cfg.mobile_autoregister) (void)_hal.after(static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(protocol::presence_probe_jitter_ms) + 1)), kMobileLayerQueryTimerId);
                }
                presence_arm_check(_presence_T_ms);                                        // one probe from ANY mobile refreshed us (suppression)
                if (_presence_prescan) presence_maybe_rehome();
                break;
            }
        }
        if (mine < 0) {                                                                     // hash ABSENT -> home dropped us (reboot/eviction) -> re-register (staggered)
            MR_EMIT("presence_roster_absent", EF_I("home", r->home_id));
            mobile_reset_registration("presence_roster_absent");
            (void)_hal.after(static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(protocol::presence_reregister_stagger_ms) + 1)), kMobileDiscoverTimerId);
        }
        return;
    }
    // a roster from ANOTHER home (leaf-free) -> a candidate-home hint (S6.4-C passive discovery, zero TX)
    presence_note_candidate(r->home_id, r->home_layer, snr_q4);
    // D14 reverse direction: if this candidate ECHOed MY probe, record HOW IT hears me (me->cand) on the candidate entry
    if (r->has_echo && r->echo_hash32 == _key_hash32)
        for (uint8_t i = 0; i < _presence_cand_n; ++i)
            if (_presence_cand[i].home_id == r->home_id && _presence_cand[i].home_layer == r->home_layer) { _presence_cand[i].echo_tier = r->echo_quality; break; }
}

// §S6.4-C: record/refresh an overheard candidate home (its heard-SNR EWMA). Skip our own current home. echo_tier=0xFF (unknown) until the candidate echoes one of our probes.
void Node::presence_note_candidate(uint8_t home_id, uint8_t home_layer, int16_t snr_q4) {
    if (!_cfg.is_mobile || home_id == 0) return;
    if (_my_mobile_reg.active && home_id == _my_mobile_reg.home_id) return;
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _presence_cand_n; ++i)
        if (_presence_cand[i].home_id == home_id && _presence_cand[i].home_layer == home_layer) {
            _presence_cand[i].snr_q4 = protocol::snr_ewma_step(_presence_cand[i].snr_q4, snr_q4);   // step() NOT update(): the slot is seeded at insertion, so no seed-if-zero (bit-identical to the old inline form)
            _presence_cand[i].last_seen_ms = now; return;
        }
    uint8_t slot = presence_cand_alloc_slot();   // §P2-6: append or evict-stalest (never clobber the best candidate)
    _presence_cand[slot] = { home_id, home_layer, snr_q4, /*echo_tier=*/0xFF, now, now };
}

// §P2-6: pick the slot for a NEW presence candidate — append while there is room, else evict the STALEST entry (min
// last_seen_ms). The old evict-slot-0 could clobber the freshest/best candidate; evict-stalest can only drop the entry
// least-recently heard. Behavior change ONLY when the table is FULL (cap_presence_candidates) — no scenario reaches it
// (proven by byte-identity across the suite: a differing eviction victim would perturb the stream).
uint8_t Node::presence_cand_alloc_slot() {
    if (_presence_cand_n < protocol::cap_presence_candidates) return _presence_cand_n++;
    uint8_t o = 0;
    for (uint8_t i = 1; i < _presence_cand_n; ++i)
        if (_presence_cand[i].last_seen_ms < _presence_cand[o].last_seen_ms) o = i;
    return o;
}

// §D16: a wrong-wire_version roster from this home -> mark the candidate INCOMPATIBLE (find-or-add). presence_maybe_rehome
// skips incompatible candidates, so a foreign-firmware home is never a re-home/DISCOVER target (spec D16 (b)).
void Node::presence_mark_incompatible(uint8_t home_id, uint8_t home_layer) {
    if (!_cfg.is_mobile || home_id == 0) return;
    for (uint8_t i = 0; i < _presence_cand_n; ++i)
        if (_presence_cand[i].home_id == home_id && _presence_cand[i].home_layer == home_layer) { _presence_cand[i].incompatible = true; return; }
    const uint64_t now = _hal.now();
    uint8_t slot = presence_cand_alloc_slot();   // §P2-6: append or evict-stalest (never clobber the best candidate)
    _presence_cand[slot] = { home_id, home_layer, /*snr_q4=*/0, /*echo_tier=*/0xFF, now, now };
    _presence_cand[slot].incompatible = true;
}

// §S6.4-C: a candidate sustainedly >= presence_rehome_tier_delta tiers better than my (weak) home, held >= candidate_hold,
// AND the anti-flap dwell elapsed -> a VOLUNTARY re-home = reset + re-DISCOVER (the FSM adopts the STRONGEST OFFER).
void Node::presence_maybe_rehome() {
    if (!_cfg.is_mobile || !_my_mobile_reg.active || !_presence_prescan) return;
    const uint64_t now = _hal.now();
    if (now - _last_adopt_ms < protocol::presence_rehome_dwell_ms) return;                  // anti-flap
    // D14 current-home bottleneck = WORSE of (home->me = my RX EWMA of its rosters) and (me->home = my roster tier).
    const uint8_t home_worst = std::min<uint8_t>(protocol::presence_quality_tier(_presence_home_rx_q4), _presence_my_tier);
    for (uint8_t i = 0; i < _presence_cand_n; ++i) {
        if (_presence_cand[i].incompatible) continue;                                       // §D16: wrong-wire_version home -> never DISCOVER at it
        if (_presence_cand[i].home_id == _my_mobile_reg.home_id) continue;
        if (_presence_cand[i].home_layer != active_layer_id()) continue;                    // same-PHY candidates only (cross-layer proactive re-home deferred)
        // D14 candidate bottleneck = WORSE of (cand->me = my RX of its roster) and (me->cand = its echo, if known).
        const uint8_t cand_rx   = protocol::presence_quality_tier(_presence_cand[i].snr_q4);
        const uint8_t cand_worst = (_presence_cand[i].echo_tier == 0xFF) ? cand_rx : std::min<uint8_t>(cand_rx, _presence_cand[i].echo_tier);
        if (cand_worst < home_worst + protocol::presence_rehome_tier_delta) continue;       // not enough better on the BOTTLENECK link (hysteresis)
        if (now - _presence_cand[i].first_seen_ms < protocol::presence_candidate_hold_ms) continue;  // not sustained
        MR_EMIT("presence_rehome", EF_I("from", _my_mobile_reg.home_id), EF_I("to", _presence_cand[i].home_id), EF_I("cand_tier", cand_worst), EF_I("home_tier", home_worst));
        mobile_reset_registration("presence_rehome");                                       // keeps home_id for the j_discover last-home block
        (void)_hal.after(0, kMobileDiscoverTimerId);                                        // re-DISCOVER -> strongest OFFER (the candidate) wins
        return;
    }
}

#endif  // MR_FEAT_MOBILE

}  // namespace MESHROUTE_NS
