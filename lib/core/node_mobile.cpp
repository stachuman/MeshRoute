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

// ★★ §MH-S3 §5.2 — EQUAL JITTER, the single mobile-side draw site. Contract + the reasoning live in node.h
// beside the declaration; the pure bounds live in protocol_constants.h beside `retry_backoff_window`.
// EXACTLY ONE `rand_range` per call, and that count is pinned by a native test — it is the whole
// attribution story of this slice, not an implementation detail (§5.2: "the draw and its order are part of
// the simulation contract").
uint32_t Node::mobile_equal_jitter(uint32_t window) {
    return static_cast<uint32_t>(_hal.rand_range(static_cast<int>(protocol::equal_jitter_lo(window)),
                                                 static_cast<int>(protocol::equal_jitter_hi_excl(window))));
}

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
    // ★★★ §MH-S4 §4.2 — THE ONE-SHOT CONSUMPTION IS GONE (`_mobile_arm_once = false` stood here). A manual
    // `mobile register` is now a DURABLE volatile request, so the FSM below is entered identically whether the
    // operator or `mobile_autoregister` asked for it: MANUAL AND AUTOMATIC STARTS SHARE ONE FSM, differing only
    // in who enters `seeking`. See `registration_armed()` / `mobile_request_home_service()` in node.h.
    // ★ §MH-S4 §4.1 — AND THIS IS WHERE `seeking` IS ENTERED. `recovering` is NOT overwritten: it means "a
    // PREVIOUSLY ATTACHED home was lost", which stays true across every DISCOVER round of the recovery, and a
    // surface that says "reconnecting to your home" must not silently degrade to "searching" on the second try.
    // ⓘ `claiming` cannot be observed here: this function returns above on `_my_mobile_reg.active`, which the
    //   provisional adopt sets, so the only reachable states at this line are `dormant`, `seeking` and
    //   `recovering` — and `dormant` is reachable only with autoregister ON (the OFF case returned at the gate).
    if (_mobile_attach_state != MobileAttachState::recovering) _mobile_attach_state = MobileAttachState::seeking;
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
        if (mobile_service_desired()) (void)_hal.after(protocol::mobile_offer_window_ms, kMobileDiscoverTimerId);   // §MH-S4 §4.2: an explicitly-requested session keeps sweeping too (was mobile_autoregister only)
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
    // ⛔ Draw-free HERE: `mobile_discover_send` itself introduces no jitter — the DISCOVER goes out at the
    // instant its caller decided on. ⚠ CORRECTED 2026-08-07 (§MH-S3-QA item 4): this line used to add "…or in
    // `mobile_admission_rejected`; S3 owns the RNG", and that second clause is NO LONGER TRUE. S3 took
    // ownership and spent the draw: `mobile_admission_rejected` (:132, the handler both `return`s below reach)
    // now draws ONE `mobile_equal_jitter(mobile_offer_window_ms)` at :149 — site D of §MH-S3's four-site draw
    // inventory. The retry this function's two rejection paths arm is therefore JITTERED, not fixed at 2000 ms.
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
    // ⚠ §MH-S4 §4.2 — `_mobile_arm_once = true` STOOD HERE ("the attempt never happened -> don't eat a manual
    // arm") and is DELETED, not moved: `_mobile_home_desired` is durable, so there is no one-shot left to
    // restore. See step 2 of the contract block in node.h.
    // ⛔ §6.4 — AND NOTHING HERE TOUCHES THE HOME-LINK PLANE, still by construction: an admission failure is a
    //    statement about OUR OWN transmitter (gate 20). `_mobile_home_link` is not named in this function.
    // Bounded retry (gate 6). ★★ §MH-S3 §5.2 — NOW JITTERED, honouring the marker S1 left here ("a FIXED
    // delay, deliberately: jittering it needs a draw and S3 is the only planned RNG re-anchor in this arc").
    // Equal jitter over the existing mid-cycle spacing constant (U1 — no second constant): rand(1000, 2001).
    // ★ THE FAILURE MODE IS §5.2'S OWN: a channel busy enough to refuse one mobile's DISCOVER refuses the
    // whole fleet's, and a FIXED 2000 ms retry marched them all back onto that channel together, forever.
    // ⚠ Unconditional on `mobile_autoregister` — unlike the no-host backoff below — because this retry is
    // servicing an authorised attempt that our own radio dropped, not autonomous behaviour the operator
    // switched off. (§MH-S4: it is reached only from the two attach sites, both of which ran
    // `registration_armed()` first, so `mobile_service_desired()` is already known true here.)
    (void)_hal.after(mobile_equal_jitter(protocol::mobile_offer_window_ms), kMobileDiscoverTimerId);
}

// Window close: pick the strongest OFFER, CLAIM its local-id, and adopt (claim-stands). No host -> exp-backoff.
void Node::mobile_claim_guard_fire() {
    if (!_cfg.is_mobile || _my_mobile_reg.active) return;
    if (_mobile_offers_n == 0) {                                   // no host on THIS PHY -> §mobile 5a: advance the scan-set; exp-backoff only after a FULL cycle
        _mobile_scan_idx = static_cast<uint8_t>((_mobile_scan_idx + 1) % scan_set_count());
        // ★★★ §MH-S3 §5.2 — EVERY NO-HOST RETRY DRAWS. THIS IS THE ARC'S ONE PLANNED RNG RE-ANCHOR.
        // Before: `delay = _mobile_backoff_ms` (or the flat mid-cycle constant) — a pure capped doubling
        // 5 s -> 120 s with no draw anywhere on the path, so mobiles powered together stayed phase-aligned
        // for as long as they ran and collided in the same OFFER window round after round (§2.2, pinned by
        // the §S0-2 characterization test). Now: the capped exponential growth is RETAINED — it still
        // computes §5.2's `window = min(5 s * 2^attempt, 120 s)` verbatim, and `_mobile_backoff_ms` is that
        // WINDOW, not the delay — and the delay is EQUAL JITTER over it: `rand(window/2, window + 1)`.
        // ★ BOTH ARMS DRAW, and that uniformity is deliberate: §5.2 says "every no-host retry", and the
        //   mid-cycle inter-PHY gap IS a no-host retry (it re-DISCOVERs, just on the next scan PHY). It
        //   jitters over its own window (`mobile_offer_window_ms`) rather than the backoff ladder, which it
        //   was never part of. ⇒ the branch's draw count is ONE, whichever arm runs — a far easier contract
        //   to pin than "one, unless the scan set has more than one entry".
        // ★ THE DRAW IS UNCONDITIONAL ON `mobile_autoregister`, while the ARM below is not. Deliberate: the
        //   `mobile_no_host` emit reports `backoff_ms`, and a reported delay that was never computed would
        //   be a display-shaped lie. It also makes the draw count independent of a config flag.
        uint32_t window;
        if (_mobile_scan_idx == 0) {                               // full cycle (or single-entry) with no host anywhere -> exp-backoff (B3)
            _mobile_backoff_ms = _mobile_backoff_ms
                ? std::min(2u * _mobile_backoff_ms, protocol::mobile_discover_backoff_max_ms)
                : protocol::mobile_discover_backoff_min_ms;
            window = _mobile_backoff_ms;
        } else {                                                   // mid-cycle -> a short inter-PHY gap so the scan sweeps promptly
            window = protocol::mobile_offer_window_ms;
        }
        const uint32_t delay = mobile_equal_jitter(window);
        // §MH-S4 §4.2: an explicitly-requested attachment session keeps retrying too — "it remains
        // seeking/recovering until success or `mobile unregister`". Was `_cfg.mobile_autoregister` alone, which
        // gave a manual `mobile register` exactly ONE no-host round and then silence.
        if (mobile_service_desired()) (void)_hal.after(delay, kMobileDiscoverTimerId);   // §console: backoff retry-DISCOVER
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
    // ★★★★ §MH-S4 §7.1 — THIS IS `claiming`, NOT `attached`, AND THE APP-FACING PUSH HAS MOVED AWAY FROM HERE.
    // The `mobile_reg{registered:true}` push that stood on the next line is now emitted in
    // `presence_ingest_roster`, at §7.1 STEP 4 — the FIRST chosen-home roster carrying our (hash, local id,
    // epoch). ⛔ THAT IS THE ENTIRE §S0-4 DEFECT: a CLAIM lost to an RX collision reached exactly this line and
    // told the app it was registered at a home that had no row for it, for ≈135 000 ms, with no re-CLAIM.
    // ★ What still happens here is §7.1 step 1's PROVISIONAL adoption — `set_identity`, `_joined`, the offered
    //   PHY — because the mobile really does have to operate under the offered local id to be answered at all.
    //   The distinction the spec draws is between OPERATING provisionally and TELLING THE APP it is registered.
    // ⓘ The stage `_mobile_offers[0]` is deliberately LEFT INTACT (`_mobile_offers_n` stays 1): it is the
    //   carrier `mobile_reclaim_send()` rebuilds the same-epoch re-CLAIM from (U2 — no second copy).
    _mobile_attach_state = MobileAttachState::claiming;
    schedule_triggered_beacon();                                  // announce the adopted id (peers re-bind on it)
    presence_on_adopt();                                          // §S6: seed the presence clocks + arm the FIRST check probe (REPLACES the re-CLAIM tick)
    if (mobile_service_desired()) (void)_hal.after(0, kMobileLayerQueryTimerId);   // §S6: first-registration layer-directory pull (the PERIODIC re-arm is retired; pull now rides dir_epoch changes + the 6-h safety pull). §MH-S4 §4.2: also for a manually-requested session.
}

// ★★★ §MH-S4 §7.1 step 5 — RE-SEND THE SAME CLAIM: same chosen host, same proposed local id, SAME EPOCH.
// "Same" is the whole requirement, and it is why this is not a call back into `mobile_claim_guard_fire()`:
// that function re-picks the strongest OFFER and INCREMENTS `_my_mobile_reg.epoch` (`++` inside the pack), so
// routing a retry through it would produce a DIFFERENT claim and defeat the home's idempotent claim-stands.
// ⓘ The frame is rebuilt from the retained stage `_mobile_offers[0]` plus the already-adopted
//   `_my_mobile_reg.epoch` — one carrier, no duplicated candidate (U2).
// ⛔ IT MUST NOT RE-ADOPT. `lbt_complete` calls `mobile_claim_adopt()` for every admitted `LbtKind::mobile_claim`,
//    and `mobile_claim_adopt` early-returns on `!_mobile_claim_pending` — which is exactly the state after the
//    first adopt CONSUMED the flag. So the flag is left FALSE here, deliberately and not by accident: a re-adopt
//    would call `presence_on_adopt()`, which RESETS `_mobile_claim_retries` to 0 and would turn the bounded
//    retry budget of §7.1 into an unbounded loop. This is asserted by a native case, not trusted.
// ⛔ NO NEW RNG DRAW: `pack_j_claim` + `tx_initiating` are draw-free, and the confirmation deadline is armed by
//    `presence_arm_check`, whose single `rand_range` this path was already going to spend on its next probe.
// ★★★★ §MH-S4b — IT NOW ANSWERS **"DID OUR OWN TRANSMITTER ADMIT IT?"**, because that is the only fact allowed to
// spend a re-CLAIM (see the contract at the declaration in node.h). `false` = a DEFINITIVE local refusal: nothing on
// the air, nothing spent. `true` = admitted OR accepted into the LBT defer ring — in flight either way.
bool Node::mobile_reclaim_send() {
    if (!_cfg.is_mobile || !_my_mobile_reg.active || _mobile_offers_n == 0) return false;   // C2: no stage -> nothing to re-send (never fabricate a CLAIM), and nothing to charge for
    const OfferCand& o = _mobile_offers[0];
    j_claim_in c{}; c.leaf_id = o.leaf_id; c.gateway_capable = false; c.is_mobile = true; c.key_hash32 = _key_hash32;
    c.proposed_node_id = o.proposed_local_id;
    c.claim_epoch = static_cast<uint8_t>(_my_mobile_reg.epoch);    // ★ SAME epoch — NOT `++` (that is what makes it the same CLAIM)
    c.chosen_host_id = o.responder_id;
    uint8_t buf[11]; const size_t n = pack_j_claim(c, std::span<uint8_t>(buf, sizeof buf));
    MR_EMIT("mobile_reclaim_tx", EF_I("home", o.responder_id), EF_I("local_id", o.proposed_local_id),
            EF_I("epoch", _my_mobile_reg.epoch), EF_I("attempt", _mobile_claim_retries));
    TxAdmission adm = TxAdmission::admitted;
    // ★★★ §B186a 2026-08-12 — `LbtKind::mobile_reclaim`, AND THIS LINE IS THE WHOLE POINT OF THE SLICE'S FIRST HALF.
    // THIS is the site that knows the frame is a RE-CLAIM (its sibling `mobile_claim_guard_fire` sends the INITIAL
    // one), so the identity is stamped HERE, by the act of sending, and travels with the frame — into
    // `DeferredLbt::kind` if it defers and into `TxParams::tag`'s high byte at the radio. ⛔ Nothing downstream has
    // to ask "was that a re-CLAIM?" after the FSM has moved on. Handling is unchanged: `lbt_complete` and node.cpp's
    // deferred-loss arm both name `mobile_reclaim` alongside `mobile_claim`.
    if (n == 0 || !tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::mobile_reclaim, _mobile_attach_gen, &adm)) {
        // §6.3/§6.4: OUR transmitter refused the retry. That is a LOCAL fact — record it as the last attempt
        // result and let the next confirmation deadline try again. ⛔ It does NOT move the home-link plane, and
        // ★★ §MH-S4b: it now consumes NO retry AT ALL. The caller used to increment before calling, so three
        //    local refusals exhausted the budget with zero frames transmitted and the node reported
        //    `claim_unconfirmed` for a home that was never asked a second time.
        _mobile_last_result = (adm == TxAdmission::defer_full) ? MobileAttemptResult::defer_full
                                                              : MobileAttemptResult::tx_rejected;
        MR_EMIT("mobile_tx_rejected", EF_S("site", "reclaim"),
                EF_S("result", adm == TxAdmission::defer_full ? "defer_full" : "tx_rejected"));
        // §3-A.1 twin of the probe-refusal line: MR_EMIT is device-stripped, so a log is the only way this reaches
        // metal — and §10 requires the local-transmitter reason to be readable there, never as a home-link verdict.
        _hal.log("re-CLAIM refused by OUR OWN transmitter — no retry consumed, the home link is NOT implicated");
        return false;
    }
    return true;
}

// ★★★★ §MH-S4b — THE DEFERRED HALF OF "A BUDGET IS SPENT BY THE ACT". See the contract at the declaration.
// Returns TRUE iff this dead deferred frame was a RE-CLAIM (⇒ handled here; the caller must not fall into the
// pre-attachment `mobile_admission_rejected` backoff).
bool Node::mobile_reclaim_deferred_rejected() {
    if (!_cfg.is_mobile) return false;
    // ★ IDENTIFY THE TRANSACTION BEFORE ACTING ON ITS CONTENTS ([[B147]]). Three facts distinguish a re-CLAIM from
    // a FIRST CLAIM, and all three are structural rather than heuristic:
    //   · `_mobile_claim_pending` is still TRUE for a first CLAIM — its ONLY clearer is `mobile_claim_adopt()`,
    //     which lives in `lbt_complete` and is NOT reached on this branch — while `mobile_reclaim_send()`
    //     deliberately never sets it (a re-adopt would reset the budget: node_mobile.cpp's own note);
    //   · a re-CLAIM only exists for an already provisionally-adopted node ⇒ `_my_mobile_reg.active`;
    //   · …and only while the attachment plane is `claiming`.
    // ⓘ A STALE re-CLAIM cannot reach here at all: `lbt_complete` cancels a frame whose `completion_gen` no longer
    //   matches `_mobile_attach_gen` and answers TRUE, so `admitted` is true and this whole arm is skipped ([[B142]]).
    if (_mobile_claim_pending || !_my_mobile_reg.active
        || _mobile_attach_state != MobileAttachState::claiming) return false;
    // ⛔ REFUND, not "don't count": `tx_initiating` answered TRUE for the defer, so the retry WAS charged — correctly,
    //    because a deferred frame is in flight. Only this definitive late refusal proves it never aired.
    if (_mobile_claim_retries > 0) --_mobile_claim_retries;
    _mobile_last_result = MobileAttemptResult::tx_rejected;   // §6.4/§10: OUR transmitter. ⛔ NOT a home-link state.
    MR_EMIT("mobile_reclaim_refunded", EF_I("home", _my_mobile_reg.home_id),
            EF_I("retries", _mobile_claim_retries));
    _hal.log("deferred re-CLAIM dropped at the radio queue — the retry budget is REFUNDED, the home link is NOT implicated");
    // ⛔ NO RE-ARM: `presence_claim_unconfirmed` already armed the next solicitation deadline when it sent this
    //    frame, so the round repeats on its own. Arming again would double the timer and double the jitter draw.
    return true;
}

// ★★★ §MH-S4 §7.1 steps 5-6 — THE ONE DECISION POINT FOR AN UNCONFIRMED CLAIM, shared by BOTH triggers §7.1
// gives, because step 6 says silence follows THE SAME BOUNDED COUNT as a roster that omits us:
//   · `why = "roster_absent"` — the chosen home rostered somebody, and not us (step 5);
//   · `why = "silence"`       — the confirmation deadline passed with no roster at all (step 6).
// Budget exhausted ⇒ RESET AND RETURN TO `seeking`, "rather than remaining falsely registered" — which is the
// sentence §S0-4 was written to make measurable.
void Node::presence_claim_unconfirmed(const char* why) {
    if (!_cfg.is_mobile) return;
    if (_mobile_claim_retries >= protocol::presence_claim_max_retries) {
        _mobile_last_result = MobileAttemptResult::claim_unconfirmed;
        MR_EMIT("mobile_claim_exhausted", EF_S("why", why), EF_I("home", _my_mobile_reg.home_id),
                EF_I("retries", _mobile_claim_retries));
        _hal.log("!! mobile CLAIM never confirmed by the home roster — returning to seeking");
        // ⛔ THE ATTACHMENT WAS NEVER CONFIRMED, so this is a return to `seeking`, NOT to `recovering`:
        //    `recovering` means "a previously ATTACHED home was lost" (§4.1) and would be a false claim of a
        //    history this node does not have. `mobile_reset_registration` derives exactly that from the
        //    `attached` state it is leaving (here: `claiming`), so no override is needed.
        mobile_reset_registration("claim_unconfirmed");
        _mobile_claim_solicited = false;                           // the session is over; the next attachment asks afresh
        (void)_hal.after(0, kMobileDiscoverTimerId);               // a FULL re-DISCOVER: a fresh OFFER round, a fresh id, a fresh epoch
        return;
    }
    // ★★★★ §MH-S4b — **THE BUDGET IS SPENT BY THE TRANSMISSION, NOT BY THE DECISION TO TRANSMIT.** The increment
    // stood on the line ABOVE this call, so a mobile whose radio refused every retry burned all three and reported
    // `claim_unconfirmed` with NOT ONE re-CLAIM on the air — the failure mode the budget exists to bound, reached
    // without using the resource it bounds. Fifth appearance of the rule in this arc ([[B84]], [[B145]]/[[B146]],
    // [[B139]], here). ⓘ A DEFERRED re-CLAIM counts (it is in flight); its late death refunds, in
    // `mobile_reclaim_deferred_rejected()`.
    if (mobile_reclaim_send()) ++_mobile_claim_retries;
    // ★★ §7.1 step 3 — AND THE NEXT DEADLINE IS A **SOLICITATION**, NOT A VERDICT. Clearing the substate is what
    // makes the following fire send the searching probe and then WAIT, instead of spending the next retry blind.
    _mobile_claim_solicited = false;
    presence_arm_check(protocol::presence_claim_solicit_ms);       // the next solicitation deadline (the ONE draw on this path)
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
    // ★★★ §MH-S4 §4.1 — THE DEREGISTRATION PUSH IS NOW GATED ON THE **ATTACHMENT** PLANE, NOT ON `active`.
    // `was_active` used to drive it, and with the §7.1 push moved to roster confirmation that would have been a
    // "success that isn't" in reverse: a mobile whose CLAIM was never confirmed never emitted `registered:true`,
    // so emitting `registered:false` when its provisional attachment collapses would tell the app a
    // registration ENDED that the app was never told had begun. ⇒ the pair is now symmetric by construction:
    // exactly one `registered:true` per confirmed attachment (guarded by `_presence_reg_confirmed`), and exactly
    // one `registered:false` per confirmed attachment that ends.
    const bool was_attached = (_mobile_attach_state == MobileAttachState::attached);
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
    // ★★★ §MH-S4 §4.1 — THE ATTACHMENT PLANE'S NEXT STATE, DERIVED FROM WHAT WE ARE LEAVING AND FROM NOTHING ELSE:
    //   · no home service desired at all  -> `dormant`     (honest: `registration_armed()` is false, so NO
    //                                                       DISCOVER will run; calling it "seeking" would render
    //                                                       a search that physically cannot happen);
    //   · we were CONFIRMED `attached`    -> `recovering`  (§4.1: "a previously attached home is lost");
    //   · otherwise                       -> `seeking`      (a provisional/unconfirmed attempt collapsed — there
    //                                                       is no attachment history to recover).
    // ⛔ THE HOME-LINK PLANE IS DELIBERATELY NOT TOUCHED HERE. It is ORTHOGONAL (§4.1) and its callers own it:
    //    `presence_probe_fire` sets `lost` BEFORE calling us (so the surface can say "home lost" with the age of
    //    the last confirmation still readable), `mobile_unregister` sets `unknown` (its definition includes "the
    //    home-service state is dormant"), and a roster confirmation sets `confirmed`. Clearing it here would
    //    erase the very evidence the §4.1 display rule asks to be rendered.
    _mobile_attach_state = !mobile_service_desired() ? MobileAttachState::dormant
                         : was_attached              ? MobileAttachState::recovering
                                                     : MobileAttachState::seeking;
    _presence_reg_confirmed = false;                              // §7.1: this attachment's confirmation is void — the next one must earn its own push
    _mobile_claim_retries   = 0;                                  // §7.1: a fresh attachment gets a fresh bounded budget
    _mobile_claim_solicited = false;                              // §MH-S4b §7.1 step 3: no solicitation is outstanding for an attachment that no longer exists
    MR_EMIT("mobile_reset", EF_S("reason", reason ? reason : ""));
    if (was_attached) { Push pu{}; pu.kind = PushKind::mobile_reg; pu.relayed = false; enqueue_push(pu); }   // §S2: home lost / dereg -> home=0,local=0,registered:false
}

// ★ §MH-S4 §4.2/§4.3 — `mobile register` (all three spellings): set the VOLATILE home-service request and enter
// `seeking`. Idempotent while already seeking/recovering — §4.3: "repeating it while already seeking is
// idempotent except that it may select another scan mode/PHY and trigger one immediate attempt" (the PHY/scan
// selection and the immediate attempt are the callers' job in node.h; this is the state half).
// ⓘ An `attached`/`claiming` node keeps its state here: §4.3 rules that repeating the verb while attached is "a
//   controlled re-evaluation, not a second parallel transaction", and the immediate `kMobileDiscoverTimerId`
//   the caller arms returns at the `_my_mobile_reg.active` guard — so the live attachment is not torn down by a
//   redundant command. `mobile unregister` is the verb that ends a session.
void Node::mobile_request_home_service() {
    if (!_cfg.is_mobile) return;
    _mobile_home_desired = true;
    if (_mobile_attach_state == MobileAttachState::dormant) _mobile_attach_state = MobileAttachState::seeking;
}

// ★ §MH-S4 §4.2/§4.3 — `mobile unregister`: end the current volatile attachment session and return to `dormant`.
// ⛔ NO DEREGISTRATION WIRE MESSAGE, deliberately (§4.3): the old home ages the row out under §9. This verb is
//    LOCAL — it must not put a frame on the air, so nothing here transmits.
// ⓘ The timers are CANCELLED rather than left to fire into a guard, because "cancel registration/presence
//   timers" is the verb's stated contract: a dormant node must be QUIET, and a pending probe/DISCOVER slot that
//   merely no-ops on entry still costs a wakeup on a battery-powered mobile.
void Node::mobile_unregister() {
    if (!_cfg.is_mobile) return;
    _mobile_home_desired = false;                                 // cleared FIRST so mobile_reset_registration derives `dormant`
    _hal.cancel(kMobileDiscoverTimerId);
    _hal.cancel(kMobileClaimGuardTimerId);
    _hal.cancel(kPresenceProbeTimerId);
    _hal.cancel(kMobileLayerQueryTimerId);
    _mobile_claim_pending = false;                                // drop any staged CLAIM (a deferred completion is cancelled by the gen bump below)
    _mobile_offers_n      = 0;
    ++_mobile_attach_gen;                                         // [[B142]]: supersede every in-flight attach completion — an unregister is a new (empty) transaction
    _mobile_home_link       = MobileHomeLink::unknown;             // §4.1: "unknown — ... or the home-service state is dormant"
    _mobile_home_confirmed_ms = 0;                                // no session, no confirmation age to render
    _mobile_last_result     = MobileAttemptResult::none;
    MR_EMIT("mobile_unregistered", EF_I("home", _my_mobile_reg.home_id));
    mobile_reset_registration("mobile_unregister");                // drops the attachment + the registered:false push iff we were attached
    // ★★★★ §MH-S4b — **THE `dormant` OVERRIDE IS GONE, AND ITS REMOVAL IS THE PROOF THE CONTRADICTION IS FIXED.**
    // §MH-S4 had to force `_mobile_attach_state = MobileAttachState::dormant` on the next line, because
    // `mobile_reset_registration` derives the state from `mobile_service_desired()` and that predicate was
    // `_cfg.mobile_autoregister || _mobile_home_desired` — STILL TRUE on an `autoregister=1` device, which is most
    // of them. So the verb reported `dormant` while the machine stayed armed: §4.3's post-condition held for one
    // member and for nothing else, no replacement DISCOVER was ever scheduled, and §MH-S4's own test had to INJECT
    // the timer this verb had just cancelled in order to watch "autonomy resume".
    // ⇒ With `_mobile_home_desired` as the effective session state (node.h), the DERIVATION is now correct on its
    //   own: the request is cleared above, `mobile_service_desired()` is false, and `mobile_reset_registration`
    //   yields `dormant` for BOTH `autoregister` values. The override is therefore not merely redundant — keeping it
    //   would hide whether the predicate was fixed at all. It is deleted, and a `mobile register` (or a reboot,
    //   §4.2's deliberate manual policy) is the only way back to `seeking`.
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
    if (mobile_service_desired()) (void)_hal.after(protocol::presence_safety_pull_ms, kMobileLayerQueryTimerId);   // §MH-S4 §4.2: also for a manually-requested session
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
    // ★ §MH-S4 §4.1/§7.1 — a FRESH attachment has no confirmation of any kind yet, so BOTH planes start honest:
    // the attachment plane is set to `claiming` by our caller (`mobile_claim_adopt`), and the home link is
    // `unknown` — §4.1's own definition, "no confirmation yet". ⛔ NOT `checking`: nothing has been measured and
    // then found wanting; a first CLAIM is not a failed check. ⛔ And the confirmation AGE is reset with it, so a
    // re-attachment can never render the PREVIOUS home's confirmation age as if it belonged to the new one.
    _mobile_home_link         = MobileHomeLink::unknown;
    _mobile_home_confirmed_ms = 0;
    _mobile_claim_retries     = 0;                                 // §7.1: a fresh attachment gets the full bounded re-CLAIM budget
    _mobile_claim_solicited   = false;                             // §7.1 step 3: we have not asked yet — the next deadline SENDS the solicitation
    _last_adopt_ms = _hal.now();
    // ★★ §MH-S5 §8.2 — **"RETAIN OTHER FRESH CANDIDATES AFTER AN ADOPT; REMOVE/UPDATE THE CHOSEN HOME INSTEAD OF
    // CLEARING THE TABLE."** `_presence_cand_n = 0` stood here — every adopt threw away the whole passively-gathered
    // table, so a mobile that had just re-homed was blind to every alternative it had already heard and had to
    // rediscover them from scratch. That is the opposite of §8.1's "collected passively … zero transmissions": the
    // cheapest knowledge in the plane was being discarded at the one moment it is most likely to be needed again.
    // ⇒ the chosen home's own row is REMOVED (it is no longer a *candidate*, it is the home — and
    //   `presence_note_candidate` skips it from now on), and every OTHER row is kept subject to §8.2's freshness,
    //   which `presence_maybe_rehome` enforces at selection.
    // ⛔ STALE ROWS ARE DROPPED HERE TOO, not left for the evict-stalest allocator: an entry older than
    //    `mobile_liveness_ms` can never be selected again (the §8.2 gate below refuses it) but it WOULD still
    //    occupy one of the eight slots against a genuinely fresh candidate.
    {
        const uint64_t now_a = _hal.now();
        uint8_t keep = 0;
        for (uint8_t i = 0; i < _presence_cand_n; ++i) {
            if (_presence_cand[i].home_id == _my_mobile_reg.home_id) continue;               // the chosen home: not a candidate
            if (now_a - _presence_cand[i].last_seen_ms >= protocol::mobile_liveness_ms) continue;   // §8.2: unusable, so do not hold a slot
            if (keep != i) _presence_cand[keep] = _presence_cand[i];
            ++keep;
        }
        _presence_cand_n = keep;
    }
    // ★★★★ §MH-S4b §7.1 step 3 — **THE CONFIRMATION DEADLINE IS SHORT, AND IT IS NOT THE STEADY CHECK PERIOD.**
    // §MH-S4 armed `_presence_T_ms` here (120 000 ms at the `ok` tier), so a mobile whose CLAIM was lost sat
    // provisionally attached for two minutes before anything asked, and §7.1 step 3's "short jittered searching
    // P-probe" was not implemented at all. `presence_claim_solicit_ms` is that probe's delay; `_presence_T_ms` keeps
    // its `ok` seeding above and takes over the moment a roster confirms us (`presence_ingest_roster` re-arms with it).
    // ⓘ SAME DRAW: `presence_arm_check` makes the one pre-existing `presence_probe_jitter_ms` draw either way.
    presence_arm_check(protocol::presence_claim_solicit_ms);
}

// (Re)arm the check timer at now + delay + jitter (LBT desync).
// ★ §MH-S4 §4.2 — GATED ON `mobile_service_desired()`, NOT on `_cfg.mobile_autoregister`. The old comment
// ("app-driven mode: the companion arms probes") described a policy §4.2 overturns by name: "once an attachment
// session was explicitly started, confirmation, presence checks, candidate monitoring, proactive re-home and
// home-loss recovery continue independently of this initial-auto flag". With the old gate a manually-attached
// mobile got NO presence plane at all — no confirmation deadline (so §7.1 could never time out), no home-loss
// detection, no candidate canvass — which is what gate items 9/10 test for.
// ⓘ CORPUS-INERT: `_mobile_home_desired` is only ever set by the `mobile register` console verbs, which no
//   scenario can drive, so the predicate equals `_cfg.mobile_autoregister` throughout the corpus and the
//   `rand_range` below is drawn on exactly the same occasions as before.
void Node::presence_arm_check(uint32_t delay_ms) {
    if (!_cfg.is_mobile || !mobile_service_desired()) return;
    const uint32_t jitter = static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(protocol::presence_probe_jitter_ms) + 1));
    (void)_hal.after(delay_ms + jitter, kPresenceProbeTimerId);
}

// ★★★★ §MH-S5b-ii §8.3 — **THE *ONE* PERMITTED REASON, TODAY, FOR THE STEADY CHECK PROBE TO BE A *SEARCHING* ONE.**
// The contract, the airtime argument and the trigger-3 exclusion are documented at the declaration (node.h); this is
// the arithmetic, and every term is EXISTING state — nothing is stored for it (D2: `sizeof(Node)` unmoved).
//
// ★ TRIGGER 2 = `_presence_miss > 0`, i.e. at least one probe has already gone unanswered and this is the retry.
//   §8.3 words it "the current home misses a check (an unanswered probe, on the way to `lost`)". ⓘ `_presence_miss`
//   counts only ADMITTED probes ([[B139]]) — a probe OUR OWN transmitter refused never sets it, so a busy channel
//   cannot start a canvass. That is the same boundary §6.4 draws, reused rather than re-spelled.
//
// ⛔⛔ **TRIGGER 1 (`_presence_prescan`, "the home's reported quality is weak or critical") IS *NOT* READ HERE, AND
//    THAT IS DELIBERATE AND CURRENT: IT IS **DEFERRED** UNDER [[B178]], NOT FORGOTTEN AND NOT DEAD.** §MH-S5b landed
//    it and MEASURED it: a weak-home mobile's check flips from `selected` to `searching`, **every** eligible home
//    answers, corpus P-roster airtime rises **+31 %**, `s07` collisions go 2775 → 3528, and **6 unique deliveries are
//    lost, all of them in `s07`** (734 → 728, under the `≥733` floor). ⇒ §MH-S5b-ii keeps trigger 2 and drops the
//    trigger-1 disjunct. **⛔ THE MOBILE THEREFORE STILL EVALUATES A SWITCH ON A WEAK HOME — what it no longer does is
//    SPEND AIRTIME ASKING.** ⓘ VERIFIED at the code rather than asserted (V1): `_presence_prescan`'s two remaining
//    readers are `presence_ingest_roster`'s `if (_presence_prescan) presence_maybe_rehome();` and
//    `presence_maybe_rehome`'s own guard — it is the gate that lets the switch EVALUATION run at all.
//    ⛔ **CANDIDATE COLLECTION IS NOT ONE OF THEM AND NEVER WAS: `presence_note_candidate` is UNCONDITIONAL on home
//    quality**, so the candidate table keeps filling from passive traffic whatever the home's tier.
//   ★★ **THE LIMITATION THIS LEAVES, STATED HERE BECAUSE THIS IS THE PREDICATE A READER FOLLOWS: A WEAK BUT
//      CONSISTENTLY RESPONDING HOME WILL NOT PROACTIVELY INITIATE CANDIDATE VERIFICATION, SO THE MOBILE CHANGES HOME
//      ONLY AFTER CONNECTIVITY BEGINS FAILING.** A verified echo can only come from a searching probe, and today only
//      a MISS (or loss, or `claiming`) sends one. ⇒ **this is a CONSERVATIVE INTERIM POLICY, NOT completed proactive
//      roaming**, and ⛔ **§8.3 is NOT satisfied** — §S6.4-C's purpose (*leave a weak home BEFORE loss*) is NOT met.
//   ★ **WHAT RETURNS, AND WHY IT IS NARROWER THAN WHAT WAS REMOVED ([[B178]]'s refined form):** a proactive searching
//      probe only when the home is weak/critical **AND** at least one candidate is fresh, compatible, passively
//      observed, still unverified, whose measured one-way quality could possibly satisfy the two-tier rule, **and**
//      whose candidate hold + anti-flap dwell are already satisfied — i.e. a canvass is only ever spent where a switch
//      could actually complete. ⛔ The broad *"weak home + any audible candidate"* form is REFUSED: in a dense
//      scenario nearly every mobile has an audible candidate, so it reproduces the same storm unchanged.
//      ⚠ [[B177]] (the beacon touch with no row-kind/freshness/epoch gate) must be fixed FIRST — its erroneous
//      refresh alters the very liveness and quality inputs the refined trigger would read.
//   ⛔ AND THE WIDER BOTTLENECK (`min(quality_tier(_presence_home_rx_q4), _presence_my_tier)`, which §8.4 uses for the
//      DELTA) STAYS REFUSED AS THE TRIGGER SHAPE: it is true in cases `presence_maybe_rehome` cannot act on, so a
//      canvass spent there buys nothing and is exactly §8.3's airtime hole.
// ⛔ `claiming` IS NOT A TRIGGER AND IS NOT TESTED HERE: a claiming mobile's probe is ALREADY searching (§MH-S4b
//    §7.1 step 3) and its deadline belongs to the confirmation budget. The caller keeps that fork ahead of this one.
// ⛔ NO CUSTODY SUPPRESSION. An earlier shape of this predicate refused to canvass until `_presence_key_confirmed`,
//    because the home ingests `ed_pub` on the `!searching` arm only — that hole is closed at the HOME (item 2's
//    shared refresh now carries custody on both arms), so suppressing here would have been a second fix for a
//    defect already fixed, and one that a permanently-unconfirmed key could have made permanent.
bool Node::presence_searching_probe_due() const {
    if (!_cfg.is_mobile || !_my_mobile_reg.active) return false;   // unattached -> the DISCOVER FSM owns the canvass
    return _presence_miss > 0;                                     // §8.3 trigger 2 — ⛔ trigger 1 DEFERRED ([[B178]]), see above
}

// The check timer fired: send a probe (unless a fresh roster already refreshed us), else escalate toward HOME LOST.
void Node::presence_probe_fire() {
    if (!_cfg.is_mobile || !_my_mobile_reg.active) return;         // unregistered -> the DISCOVER FSM owns it
    // ★★★★ §MH-S4 §7.1 step 6 — A `claiming` MOBILE'S DEADLINE IS A **CONFIRMATION** DEADLINE, NOT A LIVENESS MISS.
    // "Silence follows the same bounded re-CLAIM count." The distinction is decided HERE, once, and it changes only
    // what the deadline MEANS — the probe below still goes out either way (see the fork after the send).
    // ⛔ AND THE ORDER MATTERS (the [[B147]] lesson: identify the situation before any branch acts on it). The
    //    `_presence_miss` ladder is skipped entirely while claiming: reached first, it would consume the claiming
    //    mobile's deadlines and declare `presence_home_lost` after ≈135 s — a full re-DISCOVER for a defect that
    //    ONE same-epoch re-CLAIM heals, which is precisely the §S0-4 measurement.
    // ★ WHY A *SELECTED* PROBE CANNOT CONFIRM, AND WHAT REPLACED IT — verified in the home's own code rather
    //   than assumed (V1, `node_join.cpp:728`): a home ends `presence_ingest_probe` with *"a check probe for a hash
    //   we don't host -> ignore"*. So when our CLAIM was lost the home has NO row and will neither answer a
    //   SELECTED probe nor schedule a roster.
    //   ⛔ **AN EARLIER REVISION OF THIS BLOCK CONCLUDED FROM THAT: "silence is the only signal available here".
    //      THAT IS WITHDRAWN — it was true only while this path sent a SELECTED probe.** §MH-S4b makes the first
    //      ask a **SEARCHING** probe (`selected_home_id = 0`, `presence_claim_solicit_ms`), which *every* eligible
    //      home answers — including one that hosts nobody. ⇒ a lost CLAIM is now detected by a POSITIVE roster
    //      response arriving (or provably not arriving) inside `presence_claim_confirm_ms`, not by silence alone.
    //      See the solicitation fork below for the live rule; do not re-derive the retired one from this paragraph.
    //   ⓘ §7.1's step 5 (a roster that OMITS us) can still only fire at a home hosting SOMEBODY ELSE, and step 6
    //   covers the empty-home case. Sharing ONE budget between them is therefore not an optimisation: it is what
    //   stops the two triggers from each spending three retries on the same lost CLAIM.
    const bool claiming = (_mobile_attach_state == MobileAttachState::claiming);
    // ★★★★ §MH-S4b §7.1 steps 3+6 — **THE CONFIRMATION DEADLINE, i.e. "we asked and nobody answered".** This arm is
    // reached only when the solicitation probe below already went out and its roster window has now expired, and it
    // is the ONLY event entitled to spend a re-CLAIM. §MH-S4 sent the probe and spent the retry in ONE callback, so
    // the answer could not physically arrive first and the probe was decorative; splitting the deadline in two is
    // the fix, and `_mobile_claim_solicited` is what tells the two apart (node.h documents the substate).
    // ⛔ NO PROBE IS SENT HERE. The solicitation was the ask; asking twice per round would double the airtime of the
    //    one phase §7.1 keeps bounded, and would make "silence" unattributable to any single ask.
    // ⛔ NO `_presence_miss`, NO home-link move: nothing about this home has been measured — it may never have held
    //    a row for us — so the link stays `unknown` (§4.1) until a roster confirms it.
    // ⓘ `presence_claim_unconfirmed` OWNS the re-arm on both of its arms (retry -> the next solicitation deadline;
    //   exhaustion -> the DISCOVER), which is why this returns instead of falling through to the shared
    //   `presence_arm_check` at the bottom — arming twice would double the draw and double the timer.
    if (claiming && _mobile_claim_solicited) {
        presence_claim_unconfirmed("silence");
        return;
    }
    if (!claiming && _presence_miss > protocol::presence_probe_k_miss) {   // k_miss+1 unanswered probes -> HOME LOST (spec §S6.4-B)
        _mobile_home_link = MobileHomeLink::lost;                  // §MH-S4 §4.1: "the bounded run of presence misses failed". Set BEFORE the reset, which deliberately does not touch this plane.
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
    // ★★★★ §MH-S4b §7.1 step 3 — **WHILE `claiming` THE PROBE IS `SEARCHING` (selected = 0), AND THAT IS THE WHOLE
    // POINT OF STEP 3.** §MH-S4 sent a SELECTED probe here, which a home that MISSED THE CLAIM IS REQUIRED TO IGNORE
    // (V1, `node_join.cpp` `presence_ingest_probe`: an entry-less `!searching` probe falls through to *"a check probe
    // for a hash we don't host -> ignore"*) ⇒ the one mechanism meant to detect the miss could not detect it, and the
    // only remaining signal was our own timeout. A SEARCHING probe is answered by EVERY eligible home, so:
    //   · the chosen home that DID record our CLAIM rosters our (hash, local id, epoch)  -> step 4 CONFIRMS;
    //   · the chosen home that did NOT rosters WITHOUT us                                -> step 5, positive
    //     evidence of the miss instead of a 12-second silence;
    //   · other audible homes echo, which is §8's candidate canvass for free (zero extra transmissions).
    // §7.1 step 3 states exactly this: "Every eligible home may answer, while the chosen home's roster either proves
    // the row or proves absence."
    // ⓘ KEY CUSTODY IS NOT DELAYED BY THIS, checked rather than assumed: the home ingests `ed_pub` only on the
    //   `!searching` branch, so the solicitation's block is not stored — but the FIRST STEADY probe after
    //   confirmation is still SELECTED and still carries it, and `presence_ingest_roster` re-arms that probe at
    //   `_presence_T_ms` from the confirmation. Pre-§MH-S4b the first selected probe was at T after ADOPT; it is now
    //   at T after CONFIRMATION, seconds later. §S6 A.4's custody timing is therefore materially unchanged.
    // ★★★★ §MH-S5b-ii §8.3 — **THE ALREADY-SCHEDULED CHECK PROBE MAY BE A *SEARCHING* ONE, AND TODAY FOR ONE REASON.**
    // §8.3's list is closed at three; `presence_searching_probe_due()` (node.h) carries **trigger 2 ONLY** (the home
    // missed a check). ⛔⛔ **TRIGGER 1 (home quality weak/critical) IS DEFERRED UNDER [[B178]] — measured at −6
    // deliveries, all in `s07`, from a fleet-wide roster storm §8.3 itself predicts — so a weak-but-ANSWERING home is
    // never canvassed and the mobile can only leave it AFTER connectivity begins failing. That is a CONSERVATIVE
    // INTERIM POLICY, ⛔ NOT completed proactive roaming; the predicate's own block carries the full statement.**
    // ⛔ NOTHING ELSE MAY FLIP IT — §8.3 says outright that sending a
    // searching probe "merely because another node may be stronger" is "a fleet-wide roster storm bought with
    // nothing", and gate 24 asserts ZERO additional transmissions while the home is adequate.
    // ★ WHY THIS IS THE WHOLE OF THE COST: the probe is not extra and is not earlier. It is the SAME frame on the
    //   SAME deadline with ONE byte given a different value (`selected_home_id` = 0 instead of the home id), so the
    //   mobile's own airtime per unit time is byte-identical at every quality tier (gate 27). What the flip buys is
    //   §8.1's authority: a searching probe is answered by EVERY eligible home WITH AN ECHO OF US, and an echo is the
    //   only thing that can set `echo_tier` — which §8.4/item 3 now REQUIRES before a voluntary switch. ⇒ without
    //   this flip, item 3 would make voluntary re-home structurally unreachable.
    // ⛔ THE HOME STILL ANSWERS AND STILL CONFIRMS US: `presence_ingest_probe`'s searching arm schedules a roster at
    //    every eligible home including ours, and our home's roster carries our (hash, local id, epoch) triple, which
    //    is what `presence_ingest_roster` confirms on. Checked at the home's own code (V1), not assumed.
    // ⛔ AND OUR ROW IS STILL REFRESHED — that is item 2, and it is not optional: `presence_ingest_probe` used to
    //    touch `last_heard_ms` on the `!searching` arm ONLY, so flipping the kind without it would stop OUR OWN
    //    probes from feeding the home's liveness clock and its per-mobile SNR EWMA (hence its roster quality tier,
    //    hence this very decision). ⓘ Measured, not assumed: a hosted mobile's periodic BEACON also touches the row
    //    (`node_beacon.cpp`), so eviction at `mobile_liveness_ms` was NOT imminent — the honest residue is the
    //    budget-skipped beacon and the frozen tier feed. See the §MH-S5b note in `simulation/BASELINE.md`.
    p_probe_in cp{};
    const bool canvass = !claiming && presence_searching_probe_due();
    if (!claiming && !canvass) { cp.selected_home_id = _my_mobile_reg.home_id; cp.selected_home_layer = _my_mobile_reg.home_leaf_id; }
    cp.key_hash32 = _key_hash32; cp.reg_epoch = static_cast<uint8_t>(_my_mobile_reg.epoch);
    if (_crypto_ready && !_presence_key_confirmed) { cp.has_pubkey = true; for (int i = 0; i < 32; ++i) cp.ed_pub[i] = _ed_pub[i]; }
    uint8_t buf[42]; const size_t n = pack_p_probe(cp, std::span<uint8_t>(buf, sizeof buf));
    // ★★★★ [[B139]] FIXED HERE — §MH-S4 §6.4 / gate 20. THE DEFECT, exactly: `++_presence_miss` stood
    // UNCONDITIONALLY on the line after this send, DISCARDING `tx_initiating`'s result. So a probe that OUR OWN
    // TRANSMITTER refused — a full 4-slot LBT defer ring (`defer_full`) or a `DeviceHal::tx` rejection — was
    // counted as *"the home did not answer"*, and `presence_probe_k_miss + 1` such refusals walked the home-link
    // plane all the way to `presence_home_lost` -> `mobile_reset_registration` -> a full re-DISCOVER cycle. A busy
    // channel could therefore deregister a mobile from a home that was working perfectly.
    // ★ IT IS THE FOURTH ADMISSION SITE, AND THE ONLY ONE §6.4's OWN TEXT DOES NOT ENUMERATE (§6.4 names
    //   DISCOVER / OFFER / CLAIM — all three pre-attachment and touching no home-link state). This one IS the
    //   home-link plane, which is why S1 deliberately left it to S4 and why it is registered as B139.
    // ⛔ GATING ON THE BOOL IS EXACTLY RIGHT AND WAS CHECKED, NOT ASSUMED: `tx_initiating` returns TRUE for a
    //    frame merely ACCEPTED INTO the LBT defer ring (node_mac.cpp) — that probe WILL be transmitted, so it is
    //    a genuine unanswered probe and must still count. It returns FALSE only when the frame was DEFINITIVELY
    //    refused by our own radio, which is precisely the set §6.4 says must move nothing.
    // ⓘ THE EMIT IS DELIBERATELY LEFT WHERE IT WAS, before the send, and this is an attribution decision rather
    //   than an oversight: `presence_probe_tx` still means "asked". Moving it after `tx_initiating` would reorder
    //   the event stream relative to the events `tx_initiating` itself raises on EVERY probe, and would therefore
    //   move corpus rows on nodes where no probe was ever refused — destroying the property that every mover in
    //   this slice is attributable to the BEHAVIOUR change. §10's scheduled-vs-admitted distinction is instead
    //   carried by the PAIR (`presence_probe_tx` + `presence_probe_refused`), the same shape §MH-S1b gave the
    //   host OFFER with `mobile_offer_scheduled` / `mobile_offer_tx`. Stated as a residual, not hidden.
    TxAdmission adm = TxAdmission::admitted;
    bool admitted = false;
    // ⓘ §MH-S5b — THE `searching` FIELD IS NOW DERIVED FROM THE FRAME, NOT FROM `claiming`. `searching` IS
    //   `selected_home_id == 0` on the wire (`p_probe_out::searching()`, frame_codec.h), so reading the packed value
    //   is the ONE definition; `claiming ? 1 : 0` was equivalent only while `claiming` was the only reason to omit
    //   the home. ⛔ No new emit site and no new field — the same instrument, told the truth.
    if (n) { MR_EMIT("presence_probe_tx", EF_I("searching", cp.selected_home_id == 0 ? 1 : 0));
             admitted = tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0, &adm); }
    if (admitted && claiming) {
        // ★★★★ §MH-S4b §7.1 step 3 — **THE SOLICITATION IS OUT; NOW WAIT FOR ITS ANSWER.** This is the half
        // §MH-S4 was missing: it spent a re-CLAIM on this very line, in the same callback as the probe, so the
        // roster could not arrive first. The budget is untouched here — the verdict belongs to the CONFIRMATION
        // deadline armed below, which lands in the `_mobile_claim_solicited` arm at the top of this function.
        // ⛔ NO `++_presence_miss` AND NO HOME-LINK MOVE: nothing about this home has been measured yet — it may
        //    never have had a row for us at all — so calling the link `checking`/`lost` would attribute our own
        //    lost CLAIM to the radio path. The home link stays `unknown` (§4.1) until a roster confirms it.
        // ⓘ A REFUSED solicitation deliberately does NOT set the substate: it falls through to the shared
        //   `presence_arm_check(presence_probe_retry_ms)` below and ASKS AGAIN in 5 s, spending no budget — §6.4's
        //   rule that a local transmitter fact costs the attachment nothing. ⚠ RESIDUAL, stated: that retry is
        //   unbounded in TIME (a permanently blocked radio keeps a mobile `claiming` for ever) but consumes ZERO
        //   airtime and ZERO budget. §7.1 bounds the number of re-CLAIM FRAMES, which is what it can bound; a
        //   wall-clock attachment timeout would be a new mechanism the spec does not ask for.
        // ⛔ `presence_maybe_rehome()` is deliberately NOT reached while claiming: it is guarded on
        //    `_presence_prescan`, which only a ROSTER can set, so it could not fire here anyway — but stating it
        //    keeps the "one action per deadline" reading true rather than accidentally true.
        _mobile_claim_solicited = true;
        presence_arm_check(protocol::presence_claim_confirm_ms);
        return;
    }
    if (admitted) {
        ++_presence_miss;
        _mobile_home_link = MobileHomeLink::checking;               // §4.1: a confirmation is now DUE — an outstanding probe, not a failure
    } else {
        // §6.4/§10: OUR transmitter, not the home. It belongs in `last result` and NOWHERE ELSE — ⛔ the miss
        // counter is untouched, so the plane cannot walk toward `checking`/`lost`, and the plane assignment above
        // is deliberately inside the `admitted` arm rather than before the branch.
        _mobile_last_result = (adm == TxAdmission::defer_full) ? MobileAttemptResult::defer_full
                                                              : MobileAttemptResult::tx_rejected;
        MR_EMIT("presence_probe_refused", EF_S("result", adm == TxAdmission::defer_full ? "defer_full" : "tx_rejected"),
                EF_I("home", _my_mobile_reg.home_id), EF_I("miss", _presence_miss));
        // §3-A.1 twin of `mobile_admission_rejected`'s line: MR_EMIT is device-stripped, so a log line is the only
        // way this reaches metal. Not `!!`-prefixed — it is self-healing on the next check period.
        _hal.log("presence probe refused by OUR OWN transmitter — the home link is NOT implicated");
    }
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
                // ★★★★ §MH-S4 §7.1 — IDENTITY IS THE **TRIPLE** `(mobile_hash, local_id, reg_epoch)`, NOT ONE FIELD.
                // The local-id arm is NEW in this slice and it closes this arc's most persistent category error
                // (hash alone -> [[B147]]; `seq` without `InboxKind` -> [[B133]]; `LbtKind` alone -> [[B142]]).
                // §4.1 names all three as the authority for the attachment plane, so a two-of-three match MUST NOT
                // confirm — and a native case proves each of the three two-of-three combinations does not.
                // ⓘ WHY IT CAN DISAGREE AT ALL: the home is the authority for the local id. If our CLAIM was lost
                //   and the home later recorded us under a different id (a race the S2 reservation narrows but the
                //   DENY backstop can still resolve differently), a roster carrying our hash+epoch under ANOTHER id
                //   is evidence that our adopted id is WRONG — not evidence that we are attached. Confirming it
                //   would leave us routing under an id the home does not associate with us: the exact
                //   "a success that isn't" shape, one layer down.
                // ⇒ FAIL LOUD (C2) and re-register, the same shape the epoch arm above already uses.
                if (e->local_id != _my_mobile_reg.my_local_id) {
                    MR_EMIT("presence_local_id_mismatch", EF_I("home", r->home_id),
                            EF_I("rostered", e->local_id), EF_I("mine", _my_mobile_reg.my_local_id));
                    _hal.log("!! home roster carries our hash under a DIFFERENT local id — re-registering");
                    mobile_reset_registration("presence_local_id_mismatch"); (void)_hal.after(0, kMobileDiscoverTimerId); return;
                }
                // hash + local id + epoch match -> liveness refreshed BOTH directions
                _my_mobile_reg.last_heard_home_ms = _hal.now();
                _presence_miss = 0;
                // ★★★★ §MH-S4 §7.1 step 4 — THE ONE PLACE ATTACHMENT IS CONFIRMED, AND THE ONE PLACE THE APP IS
                // TOLD. A matching roster confirms BOTH planes at once (§7.1's closing paragraph, gate 21): it is
                // authoritative ATTACHMENT evidence (the triple) *and* a correlated bidirectional exchange (the
                // home answered our probe, so each direction is proven), so it also sets the HOME LINK to
                // `confirmed` and stamps the age §4.1 requires every surface to render.
                const bool was_confirmed  = _presence_reg_confirmed;       // ★ READ #1 of the two that make the flag load-bearing (see node.h)
                // ★★★ §MH-S4b — SNAPSHOT THE RE-CLAIM COUNT **BEFORE** RELEASING IT. §MH-S4 cleared
                // `_mobile_claim_retries` on the line above its own `mobile_attach_confirmed` emit, so the event's
                // `reclaims` field was STRUCTURALLY ALWAYS 0 — an instrument that cannot fail, and therefore cannot
                // report the one thing it exists to report: that this attachment was HEALED by a re-CLAIM rather than
                // confirmed first time. (Same family as [[B115]]'s display reading different state from the bound.)
                const uint8_t reclaims_spent = _mobile_claim_retries;
                _presence_reg_confirmed   = true;
                _mobile_home_link         = MobileHomeLink::confirmed;
                _mobile_home_confirmed_ms = _hal.now();
                _mobile_claim_retries     = 0;                                             // §7.1: the CLAIM landed — release the retry budget
                _mobile_claim_solicited   = false;                                         // §MH-S4b: the solicitation was ANSWERED — the substate's whole purpose (answered vs still waiting)
                // ⛔ ONCE PER ATTACHMENT. A healthy home rosters every T (60-480 s); promoting and pushing on
                //    every one of those would spam the companion with a registration event that never changed.
                //    `_presence_reg_confirmed`'s PREVIOUS value is the guard — this is read #1 of the two that
                //    make it load-bearing (node.h documents both).
                if (!was_confirmed) {
                    _mobile_attach_state = MobileAttachState::attached;
                    _mobile_last_result  = MobileAttemptResult::confirmed;
                    MR_EMIT("mobile_attach_confirmed", EF_I("home", r->home_id), EF_I("local_id", e->local_id),
                            EF_I("epoch", e->reg_epoch), EF_I("reclaims", reclaims_spent));
                    // ★ §MH-S4b §10 — "Device logs must distinguish scheduled, transmitter-admitted, and
                    // CONFIRMED." MR_EMIT is device-stripped, so the CONFIRMED third of that triple had no metal
                    // surface at all; this line is it, and it names the re-CLAIM count so a bench operator can see
                    // a healed attachment ("reclaims=2") as distinct from a clean one ("reclaims=0").
                    _hal.log(reclaims_spent ? "mobile ATTACHMENT CONFIRMED by the home roster (healed by a re-CLAIM)"
                                            : "mobile ATTACHMENT CONFIRMED by the home roster");
                    // ★ THE APP-FACING PUSH, MOVED HERE FROM `mobile_claim_adopt` (§7.1: "The current immediate
                    //   `mobile_reg{registered:true}` at OFFER-window close moves to step 4"). Field-for-field the
                    //   same event the companion already parses — home/local/home_layer/epoch — so the app needs no
                    //   change to keep working; what changed is WHEN it is true.
                    Push pu{}; pu.kind = PushKind::mobile_reg; pu.origin = _my_mobile_reg.home_id; pu.dst = _my_mobile_reg.my_local_id;
                    pu.layer_id = _my_mobile_reg.home_leaf_id; pu.ctr = _my_mobile_reg.epoch; pu.relayed = true; enqueue_push(pu);
                }
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
                    if (mobile_service_desired()) (void)_hal.after(static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(protocol::presence_probe_jitter_ms) + 1)), kMobileLayerQueryTimerId);   // §MH-S4 §4.2: also for a manually-requested session
                }
                presence_arm_check(_presence_T_ms);                                        // one probe from ANY mobile refreshed us (suppression)
                if (_presence_prescan) presence_maybe_rehome();
                break;
            }
        }
        if (mine < 0) {
            // ★★★★ §MH-S4 §7.1 step 5 — THE SAME WIRE EVIDENCE, TWO DIFFERENT ACTIONS, AND `_presence_reg_confirmed`
            // IS WHAT SEPARATES THEM. This is read #2 of the two that make the flag load-bearing (node.h).
            //   · NEVER CONFIRMED  ⇒ the CLAIM WAS NOT RECORDED. The chosen home is alive and rostering — it just
            //     has no row for us. §7.1: "re-send the same CLAIM, with the same local id and epoch, up to
            //     `presence_claim_max_retries`." ⛔ NOT a re-DISCOVER: our OFFER is still valid, the home is still
            //     the right home, and a fresh discovery round would throw away a working choice (and, pre-S2,
            //     collide with the id reservation the home is still holding for us).
            //   · PREVIOUSLY CONFIRMED ⇒ THE HOME DROPPED US (reboot / eviction). Its registry no longer holds the
            //     row it once advertised, so our local id and epoch are dead — the pre-existing staggered
            //     re-register is correct and is retained VERBATIM.
            // ⓘ ORDERING (the [[B147]] lesson): this fork is reached only AFTER the wire_version wall, the
            //   home_id/home_layer selection test and the full entry scan — i.e. the frame is fully identified as
            //   OUR CHOSEN HOME'S CURRENT roster before either branch acts on its contents.
            MR_EMIT("presence_roster_absent", EF_I("home", r->home_id), EF_I("confirmed", _presence_reg_confirmed ? 1 : 0));
            if (!_presence_reg_confirmed) { presence_claim_unconfirmed("roster_absent"); return; }
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
            // ★★ §MH-S5 §8.2 — **A CANDIDATE HEARD AFTER A `mobile_liveness_ms` GAP IS A FRESH OBSERVATION, NOT A
            // CONTINUATION.** §8.2: "if a candidate is heard after that gap, reset `first_seen_ms`, `echo_tier`, and
            // incompatibility evidence appropriate to a fresh observation", and "`first_seen_ms` alone never proves
            // sustained availability". Without this, an entry seen once an hour ago and once now satisfies the
            // 60-second sustained-availability hold (`presence_candidate_hold_ms`) on `first_seen_ms` alone — the
            // exact "an old measurement standing in for a current one" shape §8.2 exists to close.
            // ⛔ AND THE SNR IS HARD-ASSIGNED, NOT EWMA-STEPPED, ON THAT PATH: the retained EWMA describes a link
            //    that has been unobservable for 25 minutes, so smoothing the new sample into it would carry stale
            //    evidence into a switching decision. `echo_tier` goes back to 0xFF (unknown) for the same reason —
            //    a verification from before the gap is not "recent bidirectional verification" (§8.4).
            // ⓘ `incompatible` is cleared too: §D16's evidence is a wrong-`wire_version` roster, and after a gap
            //   that long the home may have been reflashed. A fresh foreign roster re-marks it immediately.
            if (now - _presence_cand[i].last_seen_ms >= protocol::mobile_liveness_ms) {
                _presence_cand[i].snr_q4       = snr_q4;
                _presence_cand[i].echo_tier    = 0xFF;
                _presence_cand[i].incompatible = false;
                _presence_cand[i].first_seen_ms = now;
                _presence_cand[i].last_seen_ms  = now;
                MR_EMIT("presence_cand_refreshed", EF_I("home", home_id), EF_I("layer", home_layer));
                return;
            }
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
        // ★★ §MH-S5 §8.2 — **FRESHNESS AT SELECTION: "reject a candidate at selection if
        // `now - last_seen_ms >= mobile_liveness_ms`".** This is the SECOND half of the freshness rule and it is not
        // redundant with the reset above: a candidate that is never heard again is never re-entered, so nothing
        // resets it — its row simply sits in the table until evicted as stalest, and `first_seen_ms` keeps
        // satisfying the 60-second hold for ever. A switch away from a working home toward a node last heard half
        // an hour ago is the failure this line refuses.
        if (now - _presence_cand[i].last_seen_ms >= protocol::mobile_liveness_ms) continue;
        // ★★★★ §MH-S5b §8.4 / item 3 — **A VOLUNTARY SWITCH REQUIRES A RECENT *VERIFIED* ECHO. AN UNVERIFIED
        // CANDIDATE IS NOW REFUSED OUTRIGHT, WHATEVER ITS LAYER.** §8.2: "voluntary switching requires a recent
        // verified echo (`echo_tier != 0xFF`), not merely an old beacon"; §8.4 lists "recent bidirectional
        // verification — a roster echo or OFFER, never a beacon alone" as one of the six conjuncts. `echo_tier` is set
        // ONLY when a candidate's roster carried an echo of OUR OWN hash (`presence_ingest_roster`), so it is exactly
        // §8.1's "authority" and it proves BOTH link directions at response time.
        // ⛔ THE OTHER FIVE CONJUNCTS ARE UNTOUCHED AND NO CONSTANT IS RE-TUNED: freshness (above), the 2-tier
        //    bottleneck delta, the 60 s hold, the 5-minute dwell and `wire_version` compatibility (`incompatible`,
        //    above) all stand exactly as they were.
        // ⛔⛔ **AND THIS SUBSUMES §MH-S5's LAYER-NIBBLE REJECTION, WHICH IS THEREFORE GONE RATHER THAN DEAD.**
        //    §MH-S5 kept `if (!verified && home_layer != active_layer_id()) continue;` and its comment said "the
        //    rejection SURVIVES for unverified ones, which is what keeps the widening safe". **That sentence is
        //    WITHDRAWN as a live rule and corrected here in place**: with the verified requirement above, no
        //    unverified candidate ever reaches the layer test, so keeping the line would have been unreachable code
        //    asserting a policy that no longer decides anything. The WIDENING it guarded still holds and is now
        //    unconditional — a VERIFIED candidate may advertise another full layer id (gate 12(b)).
        // ★ WHY "SAME-PHY" IS SATISFIED BY CONSTRUCTION rather than by a new check (V1): every entry in this table
        //   arrives through `presence_ingest_roster` / a beacon RX, i.e. it was received ON THE PHY THE RADIO IS
        //   CURRENTLY TUNED TO. So a candidate in this table is same-PHY by definition; only its layer NIBBLE can
        //   differ, and that is precisely the mixed-leaf case §8.4 wants to allow.
        // ⛔ AND THE TEAM-PHY RESTRICTION IS PRESERVED, ALSO BY CONSTRUCTION (§8.4's ⚠, gate 26): the adopt below is
        //    `reset + ordinary J discovery`, which enters `mobile_discover_fire` and is gated there by
        //    `team_phy_ok(scan_phy(_mobile_scan_idx))`. This path never retunes the radio, so it cannot move a team
        //    member off its provisioned team PHY — the widening is a LAYER-ID widening, never a PHY one.
        // ⛔ `mobile_verified_candidate_count()` IS THE ELIGIBILITY FLOOR, NOT THIS PREDICATE (node.h): it applies
        //    verification + freshness + compatibility and stops there, while this loop additionally applies the
        //    delta, the hold, the dwell and the current-home exclusion. The two are NOT to be reconciled by changing
        //    the count — a golden test pins it.
        if (_presence_cand[i].echo_tier == 0xFF) continue;
        // D14 candidate bottleneck = WORSE of (cand->me = my RX of its roster) and (me->cand = its echo). Both terms
        // are now always known, because an unverified row can no longer reach this line.
        const uint8_t cand_rx   = protocol::presence_quality_tier(_presence_cand[i].snr_q4);
        const uint8_t cand_worst = std::min<uint8_t>(cand_rx, _presence_cand[i].echo_tier);
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
