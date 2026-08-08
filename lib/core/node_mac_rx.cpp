// MeshRoute — lib/core/node_mac_rx.cpp  (R3/R4 MAC data plane — RX frame handlers)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The receive side of the RTS-CTS-DATA-ACK-NACK handshake: what we do when each
// frame type arrives. Includes the R4 anti-spam originator drop + budget-aware
// NACK (handle_rts), the §7.6 hop-budget enforcement + origin dedup + budget-hint
// ACK (handle_data), the post-ACK deliver/forward, and the sender's NACK reactions
// (LOOP_DUP cascade / BUSY_RX wait-or-requeue / HOP_BUDGET rt-bump / BUDGET blind).
// The TX/send path is in node_mac.cpp; the duty/anti-spam metric helpers are in
// node_budget.cpp. Behaviour mirrors dv_dual_sf.lua. Part of the Node class (node.h).
#include "node.h"

#include "frame_codec.h"
#include "airtime.h"
#include "identity.h"  // §P2-6: key_hash32_of (LE(ed_pub[:4]) derivation)

#include <span>

namespace MESHROUTE_NS {

// 2-bit ACK SNR bucket (dv_dual_sf.lua:842; centers -16/-8/+4) — NOT the 4-bit one.
static uint8_t bucket_of_snr_2b(int snr_q4) {
    if (snr_q4 < -192) return 0;        // < -12 dB
    if (snr_q4 <  -64) return 1;        // < -4 dB
    return 2;
}

void Node::handle_rts(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pr = parse_rts(std::span<const uint8_t>(bytes, len));
    if (!pr) return;
    const rts_out& r = *pr;
    // §mobile 6.4: a TEAM RTS addressed to our team_local_id (addr_len=1) rides the leaf-AGNOSTIC team plane — a MIXED team
    // spans leaves (an off-grid member on leaf 0 + a registered member on its home's adopted leaf) yet shares the PHY +
    // team_id. Do NOT drop it on leaf mismatch; the rest of the exchange (CTS/DATA/ACK) matches on pending state, not leaf.
    // A non-team frame, or a member whose team_local_id differs, hits the normal leaf gate -> s18/static byte-identical.
#if MR_FEAT_TEAM
    const bool team_rts_for_us = team_addr_for_us(r.next, r.addr_len);   // §P2-3
    // §P2-1 (mixed-leaf team channel): a TEAM channel-flood RTS-M (m_broadcast + mobile_src) is leaf-EXEMPT for a team member,
    // so a teammate homed on another nibble still retunes to catch the DATA-M. The RTS-M carries no team_id (only the DATA-M
    // does) so we exempt ANY team flood — the DATA-M's team_id gate (ingest_channel_m) does the actual team filtering (the same
    // "accepted residual" already documented below for the same-nibble different-team case). A static (team_id==0) is unchanged.
    const bool team_flood_rts  = r.m_broadcast && r.mobile_src && _cfg.team_id != 0;
#else
    const bool team_rts_for_us = false;   // §featuresplit: no team plane -> the normal leaf gate applies
    const bool team_flood_rts  = false;
#endif
    if (r.leaf_id != _cfg.leaf_id && !team_rts_for_us && !team_flood_rts) return;
    // §mobile: any RTS FROM our HOME (it relays our DMs onward + originates its own) proves the home is alive -> refresh
    // the home-lost clock (see handle_cts). is_mobile+active gated -> s18/static byte-identical (compiled out on a static build).
#if MR_FEAT_MOBILE
    if (_cfg.is_mobile && _my_mobile_reg.active && r.src == _my_mobile_reg.home_id)
        _my_mobile_reg.last_heard_home_ms = _hal.now();
#endif
    // R4.4 anti-spam: track this RTS in the sender's window even when it's NOT addressed to us (we
    // overhear routing-SF broadcasts) so all 1st-hop neighbours accumulate evidence. Gateway cross-layer
    // relays (RTS_FLAG_RELAY) are exempt — not a 1st-hop origination (dv:9709-9712). Keyed on the decoded
    // RTS src (frame-derived, metal-correct), NOT meta.src_hint (the sim PHY oracle, -1 on hardware).
    // M_BROADCAST RTS (a channel-gossip re-broadcast) is exempt too — a holder relaying a channel msg
    // is not a DM originator; counting it would DM-throttle honest gossipers (Lua dv:9709 `elseif
    // r.m_broadcast`). The become_free self-cap already exempts M_BROADCAST (Inc 4); this is the
    // RTS-observation half. Draw-free + inert until M_BROADCAST RTS flows (Phase 2 channel responder).
    // ✔ §rts-cr-overhear (2026-07-27) — CONVERTED, and this site was MISSED by the audit that scoped the
    // slice: it bills at the sender's CR via airtime_routing_ms(), which calls active_cr() INSIDE (node_mac.cpp),
    // so a grep for "active_cr()" does not list it. It is the RTS half of the SAME anti-spam ledger whose DATA
    // half is converted at handle_data below — one ledger, one quantity ("airtime this sender imposed on us"),
    // so billing the two halves at different coding rates would be incoherent. Expanded inline rather than
    // through airtime_routing_ms because only the CR differs (SF/BW are ours: we received it on our routing SF).
    if (!(r.rts_flags & RTS_FLAG_RELAY) && !r.m_broadcast && !r.mobile_src)   // §mobile 3b A1: a mobile_src RTS's src is a LOCAL id, not a global identity -> skip the src-keyed track (accountability rides origin=home_id)
        track_originator_observation(r.src, /*kind=rts*/0, r.ctr_lo,
                                     static_cast<uint32_t>(airtime_ms(_cfg.routing_sf, active_bw_hz(),
                                         rts_cr_decode(r.cr_adv), protocol::preamble_sym,
                                         static_cast<uint16_t>(len))));
    // Learn the RTS sender as a 1-hop neighbour — any RTS, overheard or addressed (Lua learn_rx_source).
    // §mobile 3b A1 (the load-bearing collision fix): NEVER learn a mobile's LOCAL id as a global neighbour — it can
    // collide a global id, and then rt_find(that id) would resolve to the mobile so a mobile's E2E-ACK to the colliding
    // GLOBAL id would loop back. The mobile reaches the mesh via its home_node; its src never enters the global rt.
    // §team-parity T0: the learn PLANE is now written out at the call site instead of riding learn_direct_neighbor's
    // default argument, so T2's diff is a one-token flip that a reviewer can see. Static reduction: the explicit
    // `false` IS the previous default ⇒ identical call, on every build profile (the parameter is not MR_FEAT_TEAM-gated).
    // ✔ §team-parity T2 (spec §3/T2 row 1) — DONE below as a THIRD arm. This guard is UNCHANGED (invariant I2: a
    // mobile/team local id never enters the static _rt); T2 only gives the excluded traffic a TEAM destination.
    if (!r.mobile_src && learn_direct_neighbor(r.src, protocol::db_to_q4(meta.snr_db), false, /*team_plane=*/false)) schedule_triggered_beacon();
    // §6.4 team reverse-learn: a team RTS ADDRESSED to OUR team-local id (mobile_src + addr_len=1 + next==our team id) ->
    // its src is a reachable same-team peer. Mark it a team peer (the _team_peer bitmap that is_team_peer/route-selection
    // read) AND learn a 1-hop TEAM route (_rt_team) so we can REPLY — mirrors the beacon path (node_beacon.cpp:685-686).
    // Team routes are otherwise beacon-only, and an off-grid team's 15-min periodic beacons + join-order can miss a peer
    // entirely. Gated on r.next==_team_local_id (NOT our MOBILE local id) -> never a home last-mile; mobile_src+addr_len=1
    // keeps it off the static _rt (s18/mobile-DM sims have no such frame -> byte-identical). A NEW peer also triggers our
    // beacon (Fix a) so the peer learns us back.
#if MR_FEAT_TEAM
    else if (r.mobile_src && team_addr_for_us(r.next, r.addr_len)   // §P2-3
             && r.src != 0 && r.src != 0xFF) {
        _active->_team_peer[r.src >> 3] |= static_cast<uint8_t>(1u << (r.src & 7));   // known same-team peer (is_team_peer reads this)
        if (learn_direct_neighbor(r.src, protocol::db_to_q4(meta.snr_db), false, /*team_plane=*/true)) schedule_triggered_beacon();
    }
    // ✔ §team-parity T2 (spec §3/T2 row 1) — an OVERHEARD team RTS (mobile_src, NOT addressed to our team id) proves its
    // src is a 1-hop neighbour RIGHT NOW. Today the team plane learns a direct neighbour from exactly two events (a
    // same-team beacon, 15-min periodic; the arm above), while the static plane learns from seven — this is one of the
    // five T2 closes.
    // ★★ NARROWED vs the spec, DELIBERATELY, and this is the one T2 judgement a reviewer must check. The spec says
    // "+ team learn when the src is a same-team local id". THE RTS CARRIES NO TEAM ID — verified at frame_codec.h:289
    // (rts_in has leaf_id/src/next/ctr_lo/dst/sf_index/rts_flags/payload_len/addr_len/mobile_src/cr_adv and nothing
    // else), and the sibling `team_flood_rts` at :42 says so in as many words ("we exempt ANY team flood — the DATA-M's
    // team_id gate does the actual team filtering"). So "is this src a teammate?" is NOT DECIDABLE from an overheard
    // RTS: it could equally be a FOREIGN team's member or a plain mobile's home last-mile. Admitting such a src would
    // set _team_peer for a non-teammate, which makes rt_find(x, AUTO) shadow the static _rt for that id — the mirror of
    // the I2 leak, and s35's A2 ("_rt_team contains zero static node ids"). ⇒ the arm is restricted to a src ALREADY
    // known to be a teammate, so it REFRESHES/SHORTENS a route and NEVER ADMITS a new id. _team_peer is set only from a
    // same-team BEACON (node_beacon.cpp:765, team_id-verified) or the addressed-RTS arm above — the same trust basis.
    // MISSING (needs a wire bit or T4's team-scoped Q, NOT reachable here): admitting an UNKNOWN teammate from an
    // overheard RTS. There is no sound way to do it with today's RTS.
    else if (r.mobile_src && is_team_peer(r.src)                                       // §T2 row 1 — refresh a KNOWN teammate
             && learn_direct_neighbor(r.src, protocol::db_to_q4(meta.snr_db), false, /*team_plane=*/true)) schedule_triggered_beacon();
#endif
    // ⛔⛔ ② THE IMPLICIT-ACK FROM AN OVERHEARD FORWARD-RTS (Lua dv:9863-9893) IS **DELETED** — §B153/[[B157]],
    // 2026-08-08. It used to cancel our in-flight timers and `_pending_tx.reset()` when we overheard our own
    // next-hop forwarding "the same" DATA onward, on the theory that the hop had demonstrably decoded.
    //
    // ★★★ IT IS THE SECOND TERMINAL DECISION DERIVED FROM AN RTS, AND QA's ARGUMENT CONDEMNS IT VERBATIM.
    // The match was `next/dst/ctr_lo` + `payload_len`, and its own comment credited `payload_len` with
    // *"disambiguates a 4-bit ctr_lo wrap"* — it never did: it is a LENGTH. So an overheard forward of a
    // DIFFERENT message with the same destination and the same size satisfied it, and `_pending_tx.reset()`
    // then discarded OUR message with no `data_tx`, no emit and **no `send_failed`** — the identical silent
    // loss as the retired `already_received` gate, from the identical class of missing evidence.
    // ⇒ **RTS AUTHORIZES RECEPTION; ONLY DATA PROVES MESSAGE IDENTITY** applies here too, and no key can rescue
    // it: the frame does not carry the message's identity, so the optimization is not implementable safely.
    //
    // ⛔ MEASURED, NOT ARGUED — this is what forced the deletion. With the RTS-time gate removed, `s27` STAYED
    // RED with the same five expectations, and the cause was here: at t=362768 gateway G1 has both hosted
    // mobiles' replies queued to home 101 (origins 114 and 111, both `ctr` 2, both `payload_len` 57); the
    // origin-114 flight completes at t=362768, the origin-111 flight airs its RTS and gets no CTS (103 is busy
    // relaying the first), and at **t=363718** G1 overhears 103 forwarding the **114** message to 102 and
    // credits it to the **111** flight — which had never transmitted a DATA at all. `re-m3` lost in silence.
    //
    // WHAT REPLACES IT: nothing. The sender waits out its ACK timeout and retries — and that retry is now SAFE
    // by the same mechanism the whole slice rests on: the receiver ACKs the duplicate DATA and the DATA-level
    // `_seen_origins` dedup returns before `_post_ack`, so there is no second delivery and no second forward.
    // COST: one redundant DATA on a path where an ACK was already lost or an overhear already happened —
    // exactly the trade the retired CTS gate's removal accepts, for exactly the same reason.
    // ⚠ PRICE, STATED HONESTLY: this fired **61 times across 8 corpus scenarios** (10 of them in `s18`), so it
    // was a real airtime saving on a real path — and removing it MOVES those streams. It is removed anyway,
    // because a saving that silently destroys a message is not a saving.
    // No data SF configured (empty sf_list) -> this node is data-incapable: it can't pick a DATA SF, so it does
    // NOT CTS / retune / arm NAV (no silent fallback). The sender's DM just fails — fail loud. Control plane
    // (neighbour-learn above, beacons, routing) still runs; only data participation is refused.
    if (_cfg.allowed_sf_bitmap == 0) return;
    // ROADMAP §3: an M_BROADCAST RTS is a fire-and-forget channel re-broadcast (no CTS). ANY node that hears
    // it and LACKS the msg (by the id low-16) retunes RX to the advertised SF to catch the DATA-M — not just
    // the addressed puller. The retune-back timer restores routing_sf. Holders + gateways skip. (dv:2081/9940.)
    if (r.m_broadcast) {
        // Mid-DATA-reception: don't abandon the in-flight RX to chase a channel overhear. SF-gating keeps us off
        // the control SF while _active->_pending_rx (so normally unreachable), but guard defensively — retuning here would
        // clobber the awaited DATA, and a fresh flood-state created without a resolving retune would leak the slot.
        // The flood/M-broadcast is best-effort + repair-backstopped, so skipping it while busy is safe.
        if (_active->_pending_rx) return;
        if (r.flood) {                                       // FLOOD RTS-M (§4.2): dedup/merge/create state, then catch the DATA-M
            // COUPLE create->resolve (§8 note): only a participant ALLOCS a flood-state — else the state is
            // created but the retune that resolves it is skipped and nothing ever frees it (the gateway-leak
            // bug). Gate the whole flood handling on the SAME condition as the retune. §7 CONSUMER half: a
            // gateway+owner participates (catches the DATA-M for its owner); a pure bridge (gateway_only) +
            // a data-incapable node (no data SF) stay out.
            // §mobile 6.3: a static / non-team node does NOT participate in a TEAM channel flood (mobile_src) — no flood-state,
            // no re-flood, no retune. Keeps team traffic off the static plane. s18 has no mobile_src floods -> byte-identical.
            // §mobile ACCEPTED RESIDUAL (separation): a DIFFERENT-team member (team_id!=0) can't tell WHOSE team a mobile_src
            // RTS-M is (the RTS carries no team_id — only the DATA-M does) so it DOES alloc a flood-state + may fast-pull. But
            // ingest_channel_m team-gates the DATA-M (node_channel.cpp:191: a foreign team_id DROPS it + FREES the flood-state)
            // -> NO cross-team delivery/re-flood; the only residual is a transient foreign CHANNEL_PULL (airtime). A full fix
            // needs team_id on the RTS-M (a wire change) — deferred; the delivery-level separation already holds.
            if (!(_cfg.is_gateway && _cfg.gateway_only) && _cfg.n_layers != 2 && _cfg.allowed_sf_bitmap != 0
                && !(r.mobile_src && _cfg.team_id == 0)
                && !(_cfg.is_mobile && !r.mobile_src && !mobile_registered())) {   // §S7 T-B: an OFF-GRID mobile does NOT catch/ingest a LEAF/static flood (mobile_src==0). A REGISTERED mobile (a leaf citizen) DOES (it ingests, never re-floods — flood_forward_decision). A team flood (mobile_src==1) is gated above.
                auto fbm = rts_flood_bitmap(std::span<const uint8_t>(bytes, len), r);
                if (fbm.size() == 32) {
                    const int16_t snr_q4 = protocol::db_to_q4(meta.snr_db);
                    const bool fresh = handle_flood_rts(r, fbm.data(), snr_q4);
                    if (fresh) {                              // §4.2 step 3: retune to catch the DATA-M for a FRESH state
                        const uint8_t data_sf = select_data_sf(r.sf_index, snr_q4);
                        _hal.set_rx_sf(data_sf);
                        // The data-SF frame is the lean M frame: payload_len carries its BODY length, +M_FRAME_HDR_LEN
                        // (7) = the full on-air M frame (was +13 = the old DATA-M header). Sizing it short retunes
                        // back before the M frame's RX_DONE -> drop_sf_mismatch. +30 ideal margin + the metal slop.
                        // §mobile 6.3: a TEAM M-frame (mobile_src is the exact proxy — set IFF the frame is team-scoped)
                        // is +4 B (the team_id tail, M_FRAME_TEAM_HDR_LEN=11) -> size the window for it or the frame is
                        // dropped at data SF>=10 (the +4 B airtime exceeds the 30 ms margin).
                        const uint16_t m_hdr = r.mobile_src ? M_FRAME_TEAM_HDR_LEN : M_FRAME_HDR_LEN;
                        // ✔ §rts-cr-overhear (2026-07-27) — CONVERTED. This is an OVERHEARER's retune window for
                        // the SENDER's M frame, so the CR that sizes it is the sender's, not ours: rts_cr_decode
                        // (r.cr_adv), the same datum start_pending_rx_expiry consumes off PendingRx. With
                        // active_cr() an overhearer at cr5 retuned back BEFORE a cr8 sender's M frame finished
                        // (up to 1.6x on the payload term) and lost it to drop_sf_mismatch — the same defect that
                        // cost s32 26 data_rx_timeouts, just on the channel plane instead of the DM plane.
                        // ★ MEASURED in s33_mixed_cr_channel_overhear (cr8 gateway leaves 119/120 flood to cr5
                        // leaves): channel_msg_received 26 -> 35, channel_msg_overheard 17 -> 26, and because
                        // the overhearers now CATCH those 9 frames, channel_pull_sent 10 -> 1 — nine repair
                        // pulls that no longer have to happen, plus the contention they cost (tx_lbt_defer
                        // 17 -> 7, collision 8 -> 4). Reverting THIS ONE LINE to active_cr() puts every one of
                        // those counters back, so the win is attributable here and nowhere else. Before s33
                        // existed the site was unvalidatable, which is why §rts-cr deferred it. Twin below.
                        const uint32_t back = protocol::cts_to_data_gap_ms
                            + airtime_ms(data_sf, active_bw_hz(), rts_cr_decode(r.cr_adv), protocol::preamble_sym,
                                         static_cast<uint16_t>(r.payload_len + m_hdr)) + 30 + _hal.rx_window_slop_ms(data_sf);
                        (void)_hal.after(back, kOverhearRetuneTimerId);
                        MR_EMIT("channel_overhear_armed", EF_I("sender", r.src), EF_I("chosen_data_sf", data_sf), EF_B("flood", true));
                        // Part B YIELD (spec 2026-06-28): we retuned to grab a NEW flood while awaiting our CTS -> our
                        // next-hop likely retuned for it too, so the CTS won't arrive until the flood clears. Push our
                        // CTS-timeout past it (no retry burned) -> catch the channel msg AND keep the DM retry, instead
                        // of today's "miss the CTS on the wrong SF -> timeout -> burn a retry" with the flood caught anyway.
                        // §rts-cr-overhear: nav_duration_cts is used here only as a DATA+ACK duration estimator
                        // and the frame being estimated is THIS FLOOD's M frame, whose sender advertised its CR
                        // in the RTS we are holding. So — unlike the two genuine overheard-CTS callers — this
                        // one CAN state the peer's CR, and does. (This caller is why the CR is a parameter of
                        // nav_duration_cts rather than a hardcoded active_cr() inside it.)
                        // ⚠ CURRENTLY UNREACHABLE, and that is the honest coverage statement: the guard's first
                        // term is the compile-time constant protocol::flood_yield_grab_enable == 0 (Part B was
                        // shipped OFF — "UNTESTED, twin has no floods"). A poison probe here moves 0/30 and the
                        // corpus emits ZERO reserve_yield events. So this conversion is correct-by-inspection
                        // and inert today; it becomes live the day Part B is enabled. Nothing tests it.
                        if (protocol::flood_yield_grab_enable && _active->_pending_tx && _active->_pending_tx->awaiting_cts)
                            reserve_yield(nav_duration_cts(data_sf, static_cast<uint8_t>(protocol::reserve_est_payload_bytes),
                                                           rts_cr_decode(r.cr_adv)));
                    }
                }
            }
            return;                                          // FLOOD RTS never CTSes
        }
        if (!(_cfg.is_gateway && _cfg.gateway_only) && _cfg.n_layers != 2 && !channel_have_id_lo16(r.m_payload_id_lo16)
            && !(r.mobile_src && _cfg.team_id == 0)
            && !(_cfg.is_mobile && !r.mobile_src && !mobile_registered())) {   // §S7 T-B: an OFF-GRID mobile does not overhear a LEAF pull-response either (not a leaf member); §mobile 6.3: a static / non-team node does not overhear a TEAM pull-response (mobile_src) — §7 consumer / Principle 11: a dual-layer gateway never overhears a channel pull-response

            const uint8_t data_sf = select_data_sf(r.sf_index, protocol::db_to_q4(meta.snr_db));
            _hal.set_rx_sf(data_sf);
            // Stay on the data SF until the M frame lands: gap (RTS->DATA) + the FULL M-frame airtime
            // (r.payload_len carries the BODY length; +M_FRAME_HDR_LEN (7) covers the M header) + margin.
            // Sizing it short retunes back ~one header's airtime too early -> the M frame is dropped
            // (drop_sf_mismatch). The +30 is the sim's ideal margin; rx_window_slop_ms adds the REAL metal
            // RX_DONE/SPI turnaround (ZERO on the sim; the same slop start_pending_rx_expiry carries).
            const uint16_t m_hdr = r.mobile_src ? M_FRAME_TEAM_HDR_LEN : M_FRAME_HDR_LEN;   // §mobile 6.3: a team M-frame is +4 B (team_id tail) — size for it or it drops at data SF>=10
            // ✔ §rts-cr-overhear — the twin of the FLOOD retune above, converted with it: the M frame is the
            // SENDER's, so it is sized at the sender's advertised CR. ⚠ COVERAGE IS WEAKER HERE THAN AT THE
            // TWIN, and measured 2026-07-27 rather than assumed. This line EXECUTES ~676x corpus-wide (s15,
            // s15_metal, s17, sim_9node_base) — a poison probe here moves 5/30 scenarios — but EVERY one of
            // those senders is cr5, so the conversion is value-inert: reverting this site alone to active_cr()
            // moves NOTHING. Even s33, the mixed-CR channel scenario, reaches the FLOOD twin 28x and this site
            // exactly ONCE, from a cr5 sender. ⇒ the ONLY regression detector for the mixed-CR behaviour is
            // the native test "§rts-cr-overhear: the M_BROADCAST retune window is sized for the SENDER's CR".
            // OWED: a scenario in which a cr8 node answers a CHANNEL_PULL (an M_BROADCAST RTS-M, not a flood).
            const uint32_t back = protocol::cts_to_data_gap_ms
                + airtime_ms(data_sf, active_bw_hz(), rts_cr_decode(r.cr_adv), protocol::preamble_sym,
                             static_cast<uint16_t>(r.payload_len + m_hdr)) + 30 + _hal.rx_window_slop_ms(data_sf);
            (void)_hal.after(back, kOverhearRetuneTimerId);
            MR_EMIT("channel_overhear_armed", EF_I("id_lo16", r.m_payload_id_lo16), EF_I("sender", r.src), EF_I("target", r.next),
                    EF_I("chosen_data_sf", data_sf),        // chosen_data_sf = the advertised SF we retuned to (t69)
                    EF_I("guard_ms", static_cast<int64_t>(back)),
                    EF_B("addressed", (r.next == _node_id && ((r.addr_len == 1) == _cfg.is_mobile))));   // §mobile 3b: mark-aware addressed
        }
        return;                                          // M_BROADCAST RTS never CTSes
    }
    // §mobile 3b/6.4: addressed iff the frame targets EITHER of my plane ids. for_static = next==_node_id AND the mark
    // matches my kind (a mobile accepts addr_len=1, a static addr_len=0). for_team = next==_team_local_id AND addr_len=1
    // (a team member's team-plane id; off-grid it's the only id). A non-team node has _team_local_id==0 -> for_team false
    // -> this is byte-identical to the old `next != _node_id || (addr_len==1)!=is_mobile`.
    const bool for_static_rts = r.next == _node_id && ((r.addr_len == 1) == _cfg.is_mobile);
#if MR_FEAT_TEAM
    const bool for_team_rts   = team_addr_for_us(r.next, r.addr_len);   // §P2-3
#else
    const bool for_team_rts   = false;   // §featuresplit
#endif
    if (!for_static_rts && !for_team_rts) {   // else overheard
        // NAV (virtual carrier sense): an overheard UNICAST RTS reserves the medium for the rest of the
        // exchange (CTS+DATA+ACK) — M_BROADCAST already returned above, so this is unicast. Defer own
        // unsolicited TX until then (tx_initiating/tx_flood) so we don't step on the CTS in the silent gap.
        if (_cfg.nav_enabled) {
            const uint8_t nav_sf = (r.sf_index <= 2)
                ? select_data_sf(r.sf_index, protocol::db_to_q4(meta.snr_db))   // pinned singleton -> the exact data SF
                : max_data_sf();                                                // ANY(3) -> conservative (the receiver picks)
            // §rts-cr-overhear: the reserved DATA is the RTS SENDER's, so size it at the CR it advertised.
            // Coverage: HEAVILY executed (a poison probe here moves 14/30 scenarios) but VALUE-inert on the
            // corpus — every unicast RTS a scenario overhears comes from a cr5 sender, so reverting this to
            // active_cr() moves nothing. The mixed-CR behaviour rests on the native test
            // "§rts-cr-overhear: the NAV reservation from an overheard RTS uses the SENDER's CR".
            nav_arm(nav_duration_rts(nav_sf, r.payload_len, rts_cr_decode(r.cr_adv)));
        }
        // Part A YIELD (spec 2026-06-28): the overheard RTS TARGETS our next-hop -> it's about to be occupied -> our
        // CTS/ACK can't come. Push our pending timeout past the reserve (max-SF est, LBT-backstopped), no retry burned.
        if (protocol::reserve_yield_enable && _active->_pending_tx
            && (_active->_pending_tx->awaiting_cts || _active->_pending_tx->awaiting_ack)
            && r.next == _active->_pending_tx->next)
            // §rts-cr-overhear: same exchange, same sender -> the same advertised CR sizes the estimate.
            // ⚠ UNREACHABLE TODAY, same as the flood-yield twin: reserve_yield_enable is the compile-time
            // constant 0 (Part A shipped off). Poison probe moves 0/30; zero reserve_yield events corpus-wide.
            reserve_yield(nav_duration_rts(max_data_sf(), static_cast<uint8_t>(protocol::reserve_est_payload_bytes),
                                           rts_cr_decode(r.cr_adv)));
        return;
    }
    // NAV: virtually busy under someone else's reservation -> (optionally) ignore this (new) addressed RTS;
    // the requester is a hidden node that didn't hear the reservation and will time out + retry. Tunable
    // (nav_ignore_rts): dropping it protects the reservation but causes the requester to cascade/give up.
    if (_cfg.nav_enabled && _cfg.nav_ignore_rts && _hal.now() < _nav_until_ms) return;
    MR_EMIT("rts_rx", EF_I("from", r.src), EF_I("dst", r.dst));

    // ★★★★ §B153 (2026-08-08) — THE RTS-TIME `already_received` SHORT-CIRCUIT IS GONE, AND IT IS NOT COMING BACK.
    // What stood here looked up `_last_acked_from` and, on a hit inside a 10 s TTL, answered
    // `CTS already_received = 1`. `handle_cts` treats that as DELIVERED: `_pending_tx.reset(); become_free();
    // return;` — no DATA, no emit, no `send_failed`. So a wrong answer here DISCARDS A MESSAGE IN SILENCE.
    //
    // ★★★ WHY NO KEY CAN FIX IT — THE ARGUMENT IS INFORMATION-THEORETIC, NOT A TUNING MATTER.
    // **A 7-byte RTS cannot distinguish (a) a RETRY of message A from (b) the FIRST ATTEMPT of message B that
    // shares the same `(hop src, dst, ctr_lo, payload_len)`. Those two frames are BYTE-IDENTICAL.** ⇒ no amount
    // of receiver state and no cleverer matching can return a safe TERMINAL verdict from that frame, because the
    // information simply is not in it. ⛔ The first fix attempt widened the RTS with a 4-B `flight_id` tail; that
    // "works" only by changing the frame — and the frame never needed to carry it, because the DATA already does.
    // ★★ **THE PRINCIPLE, STATED HERE BECAUSE IT IS THE DURABLE LESSON: RTS AUTHORIZES RECEPTION; ONLY DATA
    // PROVES MESSAGE IDENTITY.** It is the sharp form of this arc's recurring error — [[B142]], [[B133]],
    // [[B147]], [[B153]] — *a terminal decision made from evidence that could not support it*.
    // ⇒ A FREE RECEIVER NOW ALWAYS: creates `PendingRx`, returns a normal CTS, and waits for the DATA.
    //
    // WHERE THE AUTHORITY LIVES INSTEAD (and it was already there, doing the real work): `handle_data`'s
    // `_seen_origins` dedup, keyed on the CANONICAL identity — the full `(origin, dst, ctr)` with all 16 bits of
    // `ctr`, or the whole 8-B cleartext nonce-seed for a CRYPTED flight. Fresh DATA -> ACK + deliver/forward +
    // record. Same message, SAME prev-hop (the real lost-ACK case) -> ACK only, and it RETURNS BEFORE
    // `_post_ack` is set, so there is no second delivery and no second forward. Same message, DIFFERENT
    // prev-hop -> the existing LOOP_DUP NACK. ⓘ Its TTL is `seen_origin_ttl_ms` = 30 s, THREE TIMES the 10 s
    // window this gate had, so the replacement is strictly MORE durable, not less.
    // ⓘ COST, so nobody re-adds the bit as an "optimization": successful traffic is UNCHANGED
    // (RTS -> CTS -> DATA -> ACK, 7-B RTS). Only lost-ACK recovery pays, and it pays ONE redundant DATA —
    // after an ACK was actually lost — instead of every hop of every message risking a wrong terminal answer.
    // ⛔ `cts_in::already_received` is RESERVED in the codec and NEVER SET by this firmware (frame_codec.h).
    //   An INBOUND one is still honoured at `handle_cts`, so a heterogeneous fleet stays interoperable and NO
    //   `wire_version` bump is needed (owner-confirmed 2026-08-08: MeshRoute is not deployed).
    // A retried RTS for the SAME flight while we still await its DATA -> re-CTS + restart
    // the expiry (dv_dual_sf.lua:218 CTS-dup) so the sender's retry gets a fresh CTS.
    // ★ §B153 — THIS BRANCH IS DELIBERATELY KEPT, AND WHAT MAKES THAT LEGITIMATE IS THAT IT IS **NOT TERMINAL**.
    // It only reissues a CTS (`already_received = false`) and restarts the DATA-wait: the sender must still send
    // the DATA, and the DATA-level dedup still adjudicates identity. So the worst case of a WRONG match here is a
    // re-CTS carrying the other flight's `chosen_data_sf` plus a restarted expiry — costing a retry, never a
    // message — whereas the block deleted above could destroy one. That asymmetry is the whole distinction.
    // ⇒ `payload_len` IS INCLUDED in the match: it is the end-to-end inner+MAC length, forwarded unchanged, so it
    // cheaply separates most colliding `ctr_lo` values. ⛔ It is NOT an identity check and must never be
    // described as one — it cannot be (see the argument above); it is a cheap filter on a non-terminal fast path.
    // ✖ THE CONSERVATIVE ALTERNATIVE WAS CONSIDERED AND REJECTED, not overlooked: drop the branch and let the
    // "busy with a DIFFERENT flight" BUSY_RX NACK below answer every retried RTS. Strictly worse here — a NORMAL
    // lost-CTS retry (exactly the case this branch exists for) would be answered with a CONGESTION verdict,
    // pushing the sender into backoff/cascade against a receiver that is in fact holding its own reception for
    // it. Keeping a non-terminal fast path costs nothing that can lose data.
    if (_active->_pending_rx && _active->_pending_rx->from == r.src && _active->_pending_rx->dst == r.dst &&
        _active->_pending_rx->ctr_lo == r.ctr_lo && _active->_pending_rx->payload_len == r.payload_len) {
        cts_in cin{}; cin.chosen_data_sf = _active->_pending_rx->chosen_data_sf;
        cin.already_received = false; cin.tx_id = for_team_rts ? team_local_id() : _node_id; cin.rx_id = r.src;
        cin.payload_len = _cfg.nav_enabled ? r.payload_len : 0;   // NAV: size the overhearer's DATA reservation
        cin.cr_adv      = r.cr_adv;   // §cts-len6-cr2: forward the RTS sender's advertised CR into byte 3's cr2 half
        uint8_t cbuf[4]; const size_t cl = pack_cts(cin, std::span<uint8_t>(cbuf, sizeof cbuf));
        tx_with_retry(cbuf, cl, static_cast<int16_t>(_cfg.routing_sf), FrameTag::cts);   // R4.5b
        MR_EMIT("cts_tx", EF_I("to", r.src), EF_B("dup", true));
        start_pending_rx_expiry(_active->_pending_rx->payload_len);
        return;
    }
    // Busy with a DIFFERENT flight. If we hold a pending_rx (receiving someone else's
    // DATA), NACK the sender with how-long-busy so it waits/requeues instead of
    // grinding rts_timeout (dv:9934). If we hold a pending_tx (sending our own), STAY
    // SILENT (dv:9962 — the busy_for estimate lied for ACK-loss-stuck nodes).
    if (_active->_pending_rx) {
        const uint64_t now = _hal.now();
        uint64_t busy_for = (_active->_pending_rx->expiry_ms > now) ? (_active->_pending_rx->expiry_ms - now) : 0;
        if (busy_for > 65535) busy_for = 65535;
        const uint32_t q = (static_cast<uint32_t>(busy_for) + protocol::nack_busy_quantum_ms - 1)
                           / protocol::nack_busy_quantum_ms;                    // ceil
        nack_in nin{}; nin.reason = protocol::nack_reason_busy_rx; nin.ctr_lo = r.ctr_lo;
        nin.payload = static_cast<uint8_t>(q > 255 ? 255 : q); nin.to = r.src; nin.mobile_to = r.mobile_src;   // §mobile: a mobile/team RTS's src is a LOCAL id -> mark the NACK
        uint8_t nbuf[4]; const size_t nl = pack_nack(nin, std::span<uint8_t>(nbuf, 4));
        MR_EMIT("nack_tx", EF_I("to", r.src), EF_I("reason", protocol::nack_reason_busy_rx),
                EF_I("busy_ms", static_cast<int64_t>(busy_for)));
        tx_initiating(nbuf, nl, static_cast<int16_t>(_cfg.routing_sf), LbtKind::nack, 0);   // R4.5 LBT (handle_rts NACK, dv:9953)
        return;
    }
    if (_active->_pending_tx) {                                   // sending our own -> silent (no NACK)
        MR_EMIT("rts_drop_pending_tx", EF_I("from", r.src));
        return;
    }

    // R4.4 anti-spam DROP (Inc 1+2): if this sender's overheard airtime over the window exceeds
    // originator_airtime_share of our duty budget, silently drop the RTS — no CTS, no NACK. Relay
    // forwards are exempt. Keyed on the decoded RTS src (frame-derived, metal-correct — NOT src_hint,
    // which is -1 on hardware). The airtime BACKSTOP is gated on a real budget: with duty disabled
    // (budget 0) there is no SHARE to enforce, so skip it (matches compute_budget_tier / check_duty_cycle).
    // R-C apparent-origination COUNT clause REMOVED (Inc 1): a missed CTS makes a forwarder look like an
    // originator -> 168 false-drops on s18; the airtime backstop is the robust half (honesty- and
    // CTS-loss-independent). app_orig/rts/cts kept as info-only emit fields.
    // e2e-ack backstop exemption (2026-07-02): an RTS marked RTS_FLAG_E2E_ACK (its pending DATA is a DATA_TYPE_E2E_ACK)
    // skips the DROP — an ack must never be throttled (a throttled ack -> the sender never learns delivery -> re-send ->
    // MORE traffic). It is still OBSERVED at :40 (honest airtime metric, no bypass). Anti-spoof: a sender caught faking
    // the bit (DATA-time verify below) is flagged (e2e_ack_spoofer_flagged) and its exemption revoked for a whole window.
    // The hard duty-cycle limit still binds the sender's own ack originations (the un-spoofable ceiling).
    const bool e2e_ack_exempt = (r.rts_flags & RTS_FLAG_E2E_ACK) && !e2e_ack_spoofer_flagged(r.src);
    if (!(r.rts_flags & RTS_FLAG_RELAY) && !e2e_ack_exempt) {
        int app_orig; uint32_t total_air; uint8_t rts_n, cts_n;
        compute_originator_metric(r.src, app_orig, total_air, rts_n, cts_n);
        const uint32_t airtime_cap = static_cast<uint32_t>(
            static_cast<double>(protocol::originator_airtime_share) * _duty_cycle_budget_ms);   // floor
        const bool over_airtime = (_duty_cycle_budget_ms > 0) && (total_air > airtime_cap);
        // WARN band (Inc 2): airtime in [warn_fraction x cap, cap) -> flag, don't drop. Inc 3 carries this
        // to the sender in the ACK warn bit so an honest node backs off before the hard cap. Emitted here
        // for calibration (how close legit traffic comes to the cap).
        const uint32_t airtime_warn = static_cast<uint32_t>(
            static_cast<double>(protocol::originator_airtime_warn_fraction) * airtime_cap);
        if (_duty_cycle_budget_ms > 0 && !over_airtime && total_air > airtime_warn) {
            MR_EMIT("rts_originator_airtime_warn", EF_I("from", r.src), EF_I("ctr_lo", r.ctr_lo),
                    EF_I("airtime_ms", static_cast<int64_t>(total_air)), EF_I("warn_airtime_ms", static_cast<int64_t>(airtime_warn)),
                    EF_I("threshold_airtime_ms", static_cast<int64_t>(airtime_cap)), EF_I("window_ms", protocol::originator_window_ms));
        }
        if (over_airtime) {
            MR_EMIT("rts_drop_originator_throttle", EF_I("from", r.src), EF_I("ctr_lo", r.ctr_lo), EF_I("apparent_origination", app_orig),
                    EF_I("airtime_ms", static_cast<int64_t>(total_air)), EF_I("rts_count", rts_n), EF_I("cts_count", cts_n),
                    EF_I("threshold_count", _cfg.originator_max_per_window),
                    EF_I("threshold_airtime_ms", static_cast<int64_t>(airtime_cap)), EF_I("window_ms", protocol::originator_window_ms));
            return;                                       // silent drop (no CTS, no NACK)
        }
    }
    // R4.1 budget-aware NACK (Lua dv:10016-10044): if OUR duty budget is >=CRITICAL we likely
    // can't carry this flight to completion (CTS+DATA-RX are free but the ACK + any forward cost
    // budget), so refuse early with a BUDGET NACK -> the sender reroutes via the blind machinery
    // instead of a full RTS-CTS-DATA-ACK that stalls mid-cycle. We still pay the small NACK
    // airtime but save the CTS+ACK round-trip. STRAINED still CTSes.
    const BudgetTier my_tier = compute_budget_tier();
    if (my_tier >= BudgetTier::critical) {
        nack_in nin{}; nin.reason = protocol::nack_reason_budget; nin.ctr_lo = r.ctr_lo;
        nin.payload = static_cast<uint8_t>((static_cast<uint8_t>(my_tier) & 0x0f) << 4);   // tier HIGH nibble
        nin.to = r.src; nin.mobile_to = r.mobile_src;   // §mobile: a mobile/team RTS's src is a LOCAL id -> mark the NACK
        uint8_t nbuf[4]; const size_t nl = pack_nack(nin, std::span<uint8_t>(nbuf, 4));
        MR_EMIT("nack_tx", EF_I("to", r.src), EF_I("reason", protocol::nack_reason_budget), EF_I("tier", static_cast<uint8_t>(my_tier)));
        tx_initiating(nbuf, nl, static_cast<int16_t>(_cfg.routing_sf), LbtKind::nack, 0);   // R4.5 LBT (handle_rts NACK, dv:10043)
        return;                                          // NO CTS, NO pending_rx
    }

    const uint8_t sf = select_data_sf(r.sf_index, protocol::db_to_q4(meta.snr_db));
    PendingRx prx{}; prx.from = r.src; prx.dst = r.dst; prx.ctr_lo = r.ctr_lo;
    prx.chosen_data_sf = sf; prx.payload_len = r.payload_len; prx.set_at_ms = _hal.now();
    prx.claimed_e2e_ack = (r.rts_flags & RTS_FLAG_E2E_ACK) != 0;   // carried to DATA-time for the anti-spoof verify
    prx.mobile_from = r.mobile_src;                               // §mobile: carry the mobile-src mark -> DATA-time learn skips a mobile local id (mirror the RTS learn guard :47)
    prx.sender_cr = rts_cr_decode(r.cr_adv);                      // §rts-cr: the SENDER's CR -> start_pending_rx_expiry sizes the DATA wait for the frame actually coming, not for our own CR
    _active->_pending_rx = prx;
    start_pending_rx_expiry(r.payload_len);
    cts_in cin{}; cin.chosen_data_sf = sf; cin.already_received = false; cin.tx_id = for_team_rts ? team_local_id() : _node_id; cin.rx_id = r.src;
    cin.payload_len = _cfg.nav_enabled ? r.payload_len : 0;   // NAV: size the overhearer's DATA reservation
    // ✔ §cts-len6-cr2 — PURE FORWARDING of a datum already in hand: §rts-cr put the sender's CR in the RTS and
    // we are holding that parsed RTS, so the CTS can hand it to overhearers who never heard the RTS at all.
    // r.payload_len here is the DM quantity (inner+MAC, node_mac.cpp tx_rts) — the M_BROADCAST/FLOOD quantity
    // (inner_len-6) can NEVER reach a CTS: every path through `if (r.m_broadcast)` above returns.
    cin.cr_adv      = r.cr_adv;
    uint8_t cbuf[4]; const size_t cl = pack_cts(cin, std::span<uint8_t>(cbuf, sizeof cbuf));
    tx_with_retry(cbuf, cl, static_cast<int16_t>(_cfg.routing_sf), FrameTag::cts);   // R4.5b: stash + tag the CTS
    MR_EMIT("cts_tx", EF_I("to", r.src), EF_I("sf", sf));
    _hal.set_rx_sf(sf);                                  // NOW retune RX to hear the DATA on the data SF
}

void Node::handle_cts(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pc = parse_cts(std::span<const uint8_t>(bytes, len));
    if (!pc) return;
    const cts_out& c = *pc;
    // §mobile: a CTS from our HOME clearing OUR flight (c.tx_id=home, c.rx_id=us) proves the home is alive -> refresh the
    // home-lost clock. The mobile routes all its DMs via the home, so this fires FAR more often than the home's (possibly
    // 15-min) beacon — the beacon-only refresh (node_beacon.cpp:551) is what let a live-but-slow-beaconing home be
    // declared "lost". is_mobile+active gated -> s18/static byte-identical.
#if MR_FEAT_MOBILE
    if (_cfg.is_mobile && _my_mobile_reg.active && c.rx_id == _node_id && c.tx_id == _my_mobile_reg.home_id)
        _my_mobile_reg.last_heard_home_ms = _hal.now();
#endif
    // R4.4 anti-spam: track this CTS in the CTS sender's (c.tx_id) window (overheard, addressed to us or
    // not). CTS is the forwarder fingerprint — a legit forwarder emits ~1 CTS per inbound flight (dv:10149).
    // Unconditional now: tx_id is on the wire (no PHY-sender god-view). Dedup key is rx_id (the cleared
    // requester), not the dropped ctr_lo. Timing uses Lua CTS_LEN=4, not the 3-B C++ wire.
    // §mobile: skip the track when this CTS clears one of OUR mobile/team flights (c.tx_id is then a LOCAL id — the home
    // or teammate we are sending to). RESIDUAL (documented): a PURE-OVERHEAR mobile/team CTS (c.rx_id != us) still meters
    // a local id here — the CTS carries no mark (flags nibble full, frames.md CTS-by-context) so an overhearer can't tell.
    // THROTTLE-ONLY (a stale window entry), never a route/deliver decision -> no misroute/misdeliver; a full fix needs a
    // CTS wire bit (a flag-day, not worth it for a throttle). own_mobile_team_cts is false on s18 -> byte-identical.
    // ⚠ §rts-cr-overhear STILL MISSING HERE, and §cts-len6-cr2 does NOT close it — the reason CHANGED, so read
    // this and don't "finish the job" by grabbing c.cr_adv. The ledger's third term bills the CTS FRAME ITSELF,
    // whose sender is c.tx_id. Byte 3's cr2 is the CR of c.rx_id (the cleared node's upcoming DATA) — the WRONG
    // NODE. Using it would swap one wrong CR for another. The CTS sender's own CR is on no wire, and byte 3 is
    // now full, so closing this needs a WIDER CTS. Bounded and unchanged: a fixed 4-B frame on the routing SF
    // (single-digit ms) against a DATA term of hundreds, erring the same way (under-billing heavier peers).
    // The RTS and DATA halves of this ledger ARE billed at the sender's advertised CR.
    const bool own_mobile_team_cts = for_me_dst(c.rx_id) && _active->_pending_tx
        && (next_is_local_id(_active->_pending_tx->addr_len, _active->_pending_tx->next));
    if (!own_mobile_team_cts)
        track_originator_observation(c.tx_id, /*kind=cts*/1, /*dedup_key=*/c.rx_id,
                                     static_cast<uint32_t>(airtime_routing_ms(4)));
    if (!for_me_dst(c.rx_id)) {                           // overheard CTS (not clearing EITHER of our plane ids: node_id or team_local_id)
        // NAV: reserve the medium for the DATA+ACK this CTS just authorized (covers the hidden node near the
        // receiver that didn't hear the RTS). chosen_data_sf is exact; the length is byte 3's 4-B-quantized hint.
        // ✔ §cts-len6-cr2 (2026-07-27) — CLOSED. This was the ONE wrong-CR site that failed in the DANGEROUS
        // direction: the DATA is the CLEARED node's (c.rx_id) at ITS CR, and a cr5 overhearer sizing a cr8
        // peer's DATA released NAV mid-frame (-245 ms at the s32 shape, -1327 ms at SF12/120 B) and could then
        // transmit straight into the frame NAV existed to protect. Byte 3 now carries that CR (+ the length in
        // 4-B units), so both terms are the peer's. NO HINT (a 3-B CTS = the CTSer has NAV off) keeps the
        // unchanged fallback: a full 255-B frame at OUR CR — the only guess available, and over-reserving.
        const uint8_t peer_cr = c.payload_len ? rts_cr_decode(c.cr_adv) : active_cr();
        if (_cfg.nav_enabled) nav_arm(nav_duration_cts(c.chosen_data_sf, c.payload_len, peer_cr));
        // Part A YIELD (spec 2026-06-28): the CTS sender is OUR next-hop -> it just cleared someone else and is about
        // to receive their DATA -> busy, our CTS/ACK can't come. Push our pending timeout past the reserve (½-max est,
        // LBT-backstopped) instead of timing out blind + burning a retry during it.
        if (protocol::reserve_yield_enable && _active->_pending_tx
            && (_active->_pending_tx->awaiting_cts || _active->_pending_tx->awaiting_ack)
            && c.tx_id == _active->_pending_tx->next)
            // ✔ §cts-len6-cr2 — converted with the nav_arm above (same peer_cr). The LENGTH stays the ½-max
            // estimate, not byte 3's hint: this yield is about the CTSer being busy with SOMEONE ELSE's flight
            // whose size we deliberately guess (reserve_est_payload_bytes), LBT-backstopped.
            // ⚠ UNREACHABLE TODAY: reserve_yield_enable is the compile-time constant 0 (Part A shipped off), so
            // a poison probe here moves 0/30 and the corpus emits zero reserve_yield events. Correct by
            // inspection + inert; it goes live the day Part A is enabled.
            reserve_yield(nav_duration_cts(c.chosen_data_sf, static_cast<uint8_t>(protocol::reserve_est_payload_bytes),
                                           peer_cr));
        return;
    }
    if (!_active->_pending_tx || !_active->_pending_tx->awaiting_cts) return;   // ctr_lo flight-match dropped: rx_id==me + tx_id==next (below) pin the flight
    // Cascade disambiguation: the CTS now carries its sender (tx_id), so accept only the CTS from the
    // next-hop we RTS'd. Wire-backed (no PHY-sender god-view) — this is what distinguishes the primary
    // next-hop's CTS from an alt's when both answer the same RTS (cascade-to-alt). dv:10195.
    if (c.tx_id != _active->_pending_tx->next) return;
    // Learn the CTS sender (= our next-hop) as a 1-hop neighbour (Lua learn_rx_source / cts_frame).
    // §mobile: our next-hop on a mobile last-mile (addr_len=1) or a team DM (is_team_peer) is a LOCAL id, not a global
    // identity -> keep it OUT of the static _rt (mirror the ACK-learn guard below). Inert on s18/static (both false).
    // §team-parity T0: plane made explicit (was learn_direct_neighbor's default). Static reduction: `false` IS the
    // old default ⇒ identical call.
    // ✔ §team-parity T2 (§3/T2 row 3) — the LEARN half is DONE by the else-arm below; the guard is UNCHANGED (I2).
    if (!(next_is_local_id(_active->_pending_tx->addr_len, _active->_pending_tx->next))
        && learn_direct_neighbor(c.tx_id, protocol::db_to_q4(meta.snr_db), false, /*team_plane=*/false)) schedule_triggered_beacon();
#if MR_FEAT_TEAM
    // ✔ §team-parity T2 (§3/T2 row 3): a CTS from OUR next-hop on a TEAM flight proves that teammate hears us and is
    // 1 hop away. c.tx_id == _pending_tx->next (pinned at the `c.tx_id != next` return just above), so this learns the
    // next-hop itself. Safe by construction: is_team_peer(next) is our OWN state, so no new id is admitted.
    // ⚠ ELSE-ARM ALGEBRA (the reason the static path cannot move): next_is_local_id == (addr_len==1 || is_team_peer(next)),
    // so `!next_is_local_id` FALSE is the ONLY way to reach this arm with is_team_peer(next) true — i.e. the arm fires
    // exactly on the traffic the guard above excludes, and is unreachable whenever the guard admitted the frame.
    // team_id==0 ⇒ _team_peer is all-zero ⇒ is_team_peer false ⇒ inert (s18/static byte-identical).
    else if (is_team_peer(_active->_pending_tx->next)
             && learn_direct_neighbor(c.tx_id, protocol::db_to_q4(meta.snr_db), false, /*team_plane=*/true)) schedule_triggered_beacon();
#endif
    if (!(next_is_local_id(_active->_pending_tx->addr_len, _active->_pending_tx->next)))   // §mobile: a mobile/team next is a LOCAL id -> keep it OUT of the static bidi/liveness + route-rerank planes (mirror the CTS-learn guard above)
        note_link_confirmed(c.tx_id);                    // bidi plane: a real CTS proves our next-hop hears us -> confirmed (clears any one_way + emits link_recover)
#if MR_FEAT_TEAM
    // ★★★ ✔ §team-parity T5 (spec §3/T2 row 3 SECOND HALF — the ✖ T2 left here, now CLOSED). T2's reason for deferring
    // was exact and is now spent: "there is nothing to write it to". T5 adds team_bidi_state/team_bidi_confirmed_s to the
    // _team_liveness slot (node.h's PeerLiveness note), so a team confirm writes TEAM state and never touches the
    // node_id-indexed _link_bidi. The GUARD ABOVE IS UNTOUCHED — this is a team destination for the traffic it excludes.
    // ⚠ ELSE-ARM ALGEBRA (identical to the T2 learn arms 3 lines up, restated because it is what makes the static path
    // provably immovable): next_is_local_id == (addr_len==1 || is_team_peer(next)), so `!next_is_local_id` FALSE is the
    // ONLY way in — the arm fires exactly on the traffic the static guard rejects and is unreachable whenever the guard
    // admitted the frame. team_id==0 ⇒ _team_peer all-zero ⇒ inert (s18 / every static scenario byte-identical).
    // `c.tx_id` (not `->next`) mirrors the static arm verbatim; the two are pinned equal by the `c.tx_id != next` return
    // earlier in this function, which is the same pinning T2's learn arm documents at :496.
    else if (is_team_peer(_active->_pending_tx->next))
        note_link_confirmed(c.tx_id, /*team_plane=*/true);
#endif
    _hal.cancel(kRtsTimeoutTimerId);                     // else it fires same-tick and burns a retry
    _hal.cancel(kRetryBackoffTimerId);                   // drop a stale retry armed by a just-fired rts_timeout
    _active->_pending_tx->awaiting_cts = false;
    _active->_pending_tx->chosen_data_sf = c.chosen_data_sf;
    MR_EMIT("cts_rx", EF_I("from", _active->_pending_tx->next), EF_I("sf", c.chosen_data_sf));  // CTS is from our next-hop (src_hint=-1 on metal)
    if (c.already_received) { _active->_pending_tx.reset(); become_free(); return; }   // already delivered upstream
    (void)_hal.after(protocol::cts_to_data_gap_ms, kCtsToDataGapTimerId);     // fixed 5ms gap (NOT rand)
}

// 2026-06-09: a channel message is now the lean M frame (cmd 0xA), NOT a DATA+PAYLOAD_TYPE_M. The data SF
// frame that follows a FLOOD/M_BROADCAST RTS-M is this M frame; every node that retuned (the overhear ARM)
// ingests it promiscuously — but the STANDARD byte-0 leaf gate runs first, so a stray that punched into an
// adjacent leaf dies before buffering (the cross-leaf leak fix). No CTS/ACK/forward; the retune-back timer
// restores routing_sf. `from` keeps the DATA-M's src_hint-or-0xFF derivation (metal carries no PHY sender).
void Node::handle_channel_data(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pm = parse_m(std::span<const uint8_t>(bytes, len));
    if (!pm) return;
    if (pm->leaf_id != _cfg.leaf_id && !same_team(pm->team_id)) return;   // the leak gate — a foreign-leaf M frame dies here. §P2-1: EXEMPT a same-team M-frame (a mixed-leaf team channel crosses nibbles); ingest_channel_m still team_id-gates. Static/foreign-team -> unchanged.
    const uint8_t from = (meta.src_hint >= 0) ? static_cast<uint8_t>(meta.src_hint) : 0xFF;
    ingest_channel_m(*pm, from);
}

// Record this flight's (origin,dst,ctr) key -> expiry + prev-hop for the loop/retransmit dedup. Prune the
// expired entries first; then if still at the cap (all live) and the key is NEW, ROLL — evict the OLDEST
// (min-expiry = earliest recorded, least remaining loop-window) to make room, rather than refusing the new
// key. Re-recording an existing key just refreshes it (no eviction). Bounded by cap_seen_origins; no growth.
void Node::record_seen_origin(uint64_t sokey, uint8_t from, uint64_t now_ms) {
    for (auto it = _active->_seen_origins.begin(); it != _active->_seen_origins.end(); )
        { if (it->second <= now_ms) { _active->_seen_origin_from.erase(it->first); it = _active->_seen_origins.erase(it); } else ++it; }
    if (_active->_seen_origins.size() >= protocol::cap_seen_origins
        && _active->_seen_origins.find(sokey) == _active->_seen_origins.end()) {              // full of LIVE entries + a NEW key -> roll
        auto oldest = _active->_seen_origins.begin();
        for (auto it = _active->_seen_origins.begin(); it != _active->_seen_origins.end(); ++it)
            if (it->second < oldest->second) oldest = it;                   // min expiry = the earliest recorded
        _active->_seen_origin_from.erase(oldest->first);
        _active->_seen_origins.erase(oldest);
    }
    _active->_seen_origins[sokey]     = now_ms + protocol::seen_origin_ttl_ms;
    _active->_seen_origin_from[sokey] = from;
}

void Node::handle_data(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pd = parse_data(std::span<const uint8_t>(bytes, len));
    if (!pd) return;
    
    const data_out& d = *pd;
    // §mobile 3b/6.4: mark-aware DATA accept — addressed to EITHER plane id (for_team on a team member's team-plane id).
    // Non-team node: _team_local_id==0 -> for_team_data false -> byte-identical to the old `next != _node_id || (addr_len==1)!=is_mobile`.
    const bool for_static_data = d.next == _node_id && ((d.addr_len == 1) == _cfg.is_mobile);
#if MR_FEAT_TEAM
    const bool for_team_data   = team_addr_for_us(d.next, d.addr_len);   // §P2-3
#else
    const bool for_team_data   = false;   // §featuresplit
#endif
    if (!for_static_data && !for_team_data) return;
    if (!_active->_pending_rx || _active->_pending_rx->ctr_lo != d.ctr_lo4) return;
    // e2e-ack backstop exemption ANTI-SPOOF verify (2026-07-02): the RTS claimed RTS_FLAG_E2E_ACK (so its DROP was
    // exempted at handle_rts), but the DATA that arrived is NOT a DATA_TYPE_E2E_ACK -> the sender lied to bypass the
    // backstop. Flag it: while flagged, its RTS_FLAG_E2E_ACK is ignored (the backstop re-applies). Keyed on the PHYSICAL
    // SENDER = _pending_rx->from (the cleartext RTS src, metal-correct), NOT the sealed inner origin. One free pass, revoked.
    if (_active->_pending_rx->claimed_e2e_ack && d.type != DATA_TYPE_E2E_ACK) {
        const uint8_t spoofer = _active->_pending_rx->from;
        MR_EMIT("e2e_ack_spoof", EF_I("from", spoofer), EF_I("type", d.type));
        if (PeerLiveness* s = peer_liveness_slot(spoofer, /*create=*/true))
            s->e2e_ack_spoof_until_ms = _hal.now() + protocol::e2e_ack_spoof_penalty_ms;
    }
    // Inc 2 anti-spam: record this inbound DATA's airtime in the sender's window — the dominant
    // airtime a sender imposes on us (RTS-only never approached the cap). Keyed on _active->_pending_rx->from
    // (== this hop's RTS src, so RTS+DATA accumulate in one entry; frame-derived, metal-correct) and
    // costed at the chosen data SF over the whole frame.
    // ✔ §rts-cr-overhear (2026-07-27) — CONVERTED, and this one is an ACCOUNTABILITY fix, not a timing fix.
    // The quantity is "airtime THIS SENDER imposed on us", so it must be costed at the CR the sender actually
    // transmitted at (PendingRx.sender_cr, stashed from the RTS by handle_rts) — not active_cr(), OUR CR.
    // Billing a peer's frame at our own rate mis-meters the duty/abuse cap: a cr8 sender was under-billed by
    // up to 8/5 of its payload term against originator_airtime_share, so it could impose ~1.6x the airtime we
    // charged it before the throttle bit. The error is signed and self-serving in the wrong direction — the
    // heavier (costlier) the peer's CR, the more we under-charge it.
    if (!_active->_pending_rx->mobile_from)   // §mobile: a mobile/team DATA's src is a LOCAL id -> keep it OUT of the anti-spam ledger (mirror the RTS-anti-spam guard :40); accountability rides origin=home_id
        track_originator_observation(_active->_pending_rx->from, /*kind=data*/2, d.ctr_lo4,
            airtime_ms(_active->_pending_rx->chosen_data_sf, active_bw_hz(), _active->_pending_rx->sender_cr,
                       protocol::preamble_sym, static_cast<uint16_t>(len)));
    int oa_app_; uint32_t orig_air; uint8_t oa_rts_, oa_cts_;   // sender's windowed airtime AFTER this DATA (calibration)
    compute_originator_metric(_active->_pending_rx->from, oa_app_, orig_air, oa_rts_, oa_cts_);
    (void)oa_app_; (void)oa_rts_; (void)oa_cts_;
    // The DATA's link sender = whoever we CTS'd (_active->_pending_rx->from, set in handle_rts).
    // src_hint is the SIM oracle (real LoRa carries no PHY source; the device sets -1),
    // so use it only when present, else fall back to our pending-RX contract — else
    // from=0xFF on metal -> the ACK + HOP_BUDGET/LOOP_DUP NACKs target node 255 and the
    // dedup/loop keys are corrupt, so the DM never completes.
    const uint8_t from = (meta.src_hint >= 0) ? static_cast<uint8_t>(meta.src_hint)
                                              : _active->_pending_rx->from;
    // Parse the inner up-front so data_rx carries the (origin, ctr) message key — telemetry
    // parity with the Lua data_rx (dv:10911), which the analysis tools key delivery on. origin
    // is also needed below (BEFORE the ACK) so HOP_BUDGET/LOOP_DUP can NACK instead of re-ACKing.
    auto inner = data_inner(std::span<const uint8_t>(bytes, len), d);
    auto ui = parse_unicast_inner(inner, d.flags);
    const uint8_t origin = ui ? ui->origin : from;
    MR_EMIT("data_rx", EF_I("origin", origin), EF_I("ctr", d.ctr), EF_I("ctr_lo", d.ctr_lo4), EF_I("from", from), EF_I("dst", d.dst),
            EF_I("orig_airtime_ms", static_cast<int64_t>(orig_air)));
    // Learn the DATA prev-hop as a 1-hop neighbour (Lua learn_rx_source / data_frame).
    // §mobile: a mobile_src DATA's prev-hop `from` is a home-assigned LOCAL id -> keep it OUT of the static _rt
    // (mirror the RTS/Q guards). mobile_from==false for every static frame -> unchanged (s18 byte-identical).
    // §team-parity T0: plane made explicit (was the default). Static reduction: `false` IS the old default.
    // ✔ §team-parity T2 (§3/T2 row 4) — DONE by the else-arm below; this guard is UNCHANGED (I2).
    if (!_active->_pending_rx->mobile_from && learn_direct_neighbor(from, protocol::db_to_q4(meta.snr_db), false, /*team_plane=*/false)) schedule_triggered_beacon();
#if MR_FEAT_TEAM
    // ✔ §team-parity T2 (§3/T2 row 4): a TEAM DATA's prev-hop is a 1-hop teammate. for_team_data (:564) proves the LINK
    // is team-plane (d.next == our _team_local_id && addr_len==1) — the same acceptance edge the shipped RTS reverse-learn
    // uses — and is_team_peer pins the prev-hop as an already-known teammate, so no new id is admitted (see the row-1 note
    // on why an unknown one is not decidable from these frames).
    // ★ USE _pending_rx->from, NOT `from`. `from` prefers meta.src_hint, which is the SIM ORACLE carrying the sender's
    // STATIC protocol node_id (SimController.cpp: protocolId()); for a HOMED teammate that is NOT its team_local_id, so
    // learning `from` would install a static id in _rt_team. _pending_rx->from is the RTS's own `src`, which
    // node_mac.cpp:855 sets to the sender's team_local_id() on a team flight — frame-derived and metal-correct.
    // Static reduction: for_team_data is `false` whenever team_id==0 (team_addr_for_us, node.h:174) ⇒ inert.
    else if (for_team_data && is_team_peer(_active->_pending_rx->from)
             && learn_direct_neighbor(_active->_pending_rx->from, protocol::db_to_q4(meta.snr_db), false, /*team_plane=*/true)) schedule_triggered_beacon();
    // ✔ §team-parity T2 — DATA-ORIGIN LEARNING. The spec's §3/T2 REJECTED this on two grounds and the QA review (§10.1)
    // overruled BOTH; the owner ruled it IN. Re-verified at source here: the structs are data_in/data_out (not d_in/d_out)
    // and frame_codec.h:595 does carry `committed_hops`, a FROM-ORIGIN hop count incremented at every forward (:626 below).
    // So a real metric IS available and a REAL route installs — which sets the _team_peer bit ALONGSIDE an _rt_team entry
    // and therefore PRESERVES the node_beacon.cpp:73 invariant rather than decoupling it. U1: reuse learn_route_via, the
    // same installer the F/RREP reverse-path uses (node_route_discovery.cpp:281) — do not fork a second multi-hop install.
    //
    // METRIC. hops = committed_hops + 1, EXACT for every legal team path: an originator airs committed_hops=0 and the
    // receiver is 1 hop away, each forward adds 1. ★ THE BRIEF'S SATURATION CAVEAT DOES NOT BITE: :626 saturates the
    // value a FORWARDER airs at 7, so a receiver 8 hops out (R4's team ceiling) still reads exactly 7 → hops 8. The
    // understatement starts at 9 hops, which team_hop_cap=8 makes unreachable by discovery or DV. No cap guard is needed
    // either: committed_hops is a 3-bit wire field (frame_codec.h:483) so +1 can neither wrap nor exceed 8 — unlike the
    // F path, whose 8-bit f.hops needed the M4 guard at node_route_discovery.cpp:272.
    //
    // ★★★ ✔ §team-parity T7 (2026-07-28) — THE `is_team_peer(origin)` FENCE IS GONE, AND THIS IS THE ✖ MISSING HALF OF
    // T2's NOTE CLOSED. T2 fenced this learn because a HOMED member stamped origin = its HOME's STATIC node id
    // (stamp_origin had no team-plane exception), so "is this origin a team-plane id?" was undecidable and _rt_team[101]
    // would have put a static id in the team plane (I2's mirror, s35's A2) AND shadowed that node's own static route home.
    // ★ T6 (`9c7b40a`) REMOVED THE PREMISE, and the proof is the same two scenarios T2 cited: T2 measured origin=101 on
    // s28 node 3 (team_local_id 233) and s29 node 3 (team_local_id 196); T6's re-anchor moved exactly that inner byte,
    // origin 101 → 233/196. stamp_origin (node.h:858) now stamps team_local_id() for every flight_is_team_plane() flight.
    // RE-MEASURED HERE, not inherited: instrumenting every gate entry over all 35 scenarios yields 51 team DATA
    // receptions and **not one** plain-DM origin outside the team id space. The one surviving refusal was s35a's
    // origin=213 / prev=235 / committed_hops=2 — a member 174 dropping a route to a teammate it had never heard, i.e.
    // spec §0's bench failure itself. That is what this slice installs: `_rt_team[213] = {next 235, hops 3}`.
    //
    // ★★ WHAT NOW CARRIES THE SOUNDNESS, STATED EXACTLY — because this admits a NEW id into _rt_team/_team_peer from a
    // received DATA, which none of T2's six refresh arms do. The basis is `for_team_data` = team_addr_for_us(d.next,
    // d.addr_len) (node.h:174) = "team_id != 0 && _team_local_id != 0 && d.next == _team_local_id && addr_len == 1", i.e.
    // BOTH the RTS we CTS'd and the DATA were addressed to OUR team-plane id on the team-plane link marking. It does NOT
    // prove the SENDER is a teammate: neither RTS nor plain DATA carries a team_id (frame_codec.h:289 / :591), so a §18
    // numeric collision — a FOREIGN team's member, or a home last-miling to a hosted mobile whose local id equals our
    // _team_local_id — satisfies it too. ⇒ THE RESIDUAL IS NOT NEW: it is the identical predicate the already-shipped
    // addressed-RTS arm at :91 admits `r.src` on (its own note at :107 names the same two cases), so T7 extends an
    // accepted 1-hop admission to the multi-hop origin rather than opening a new trust basis. And in that collision the
    // frame is already CTS'd, ACKed and relayed by us (:568 accepts it on the same test) — a strictly larger containment
    // exposure than one route row, pre-existing and reported, not created here.
    // ✖ TWO NARROWER GATES WERE BUILT AND REFUSED AS DECORATION, measured not judged: `is_team_peer(_pending_rx->from)`
    // and `_pending_rx->mobile_from` are BOTH true in 51/51 corpus gate entries — the :91 arm sets the _team_peer bit for
    // exactly the src that gets us here, so the first cannot fire; and every hole either would close is already closed by
    // learn_route_via's own `via == 0 / 0xFF` sentinel guard (node_beacon.cpp:65). A guard that reads as safety and is
    // provably inert is worse than none.
    // ★ T6's FALLBACK ARM, checked independently as the one reachable static-origin path: stamp_origin takes the team
    // branch only when team_local_id() != 0, so a member whose team-DAD is still pending stamps a static/home id. `send
    // -t` is refused there (node.cpp:1141) but an AUTO send to a team peer is NOT, and _team_peer bits DO get set before
    // our own DAD completes (node_beacon.cpp:776 does not require our id) ⇒ the state IS reachable. It cannot install
    // here: that sender's RTS airs src = team_local_id() = 0 (node_mac.cpp:855), so _pending_rx->from == 0 and
    // learn_route_via returns on `via == 0` before any merge. Pinned by a native test, not by inspection.
    // ⚠ !d.app is LOAD-BEARING, not caution: parse_unicast_inner's layout applies only to a plain DM. A typed DATA builds
    // its own inner, so ui->origin is a payload byte — measured over all 35 scenarios as 14 gate entries with app=1, of
    // which s28's four read origin=4 (a hash-bind payload byte that WOULD now install, since nothing else refuses it) and
    // ten read origin=0. !d.app ⟺ type==0 (pack_data emits the TYPE byte iff type != 0). !d.crypted: §1c seals the origin,
    // so parse_unicast_inner leaves it 0 for a relay by design (frame_codec.cpp:925) — an encrypted team DM teaches
    // nothing here, deliberately (2 such entries in s22).
    // ⚠ `origin != from` is NOT load-bearing, and the brief that asked me to confirm it was overstated — MEASURED: dropping
    // it moves 0 of 36 scenarios. It keeps the 1-hop case with the else-arm above (which owns the SNR re-score and the
    // triggered beacon); without it a 1-hop team DATA re-merges an IDENTICAL candidate (same dest==via, same hops 1, same
    // score from the same meta.snr_db) microseconds after that arm installed it, so rt_merge returns MergeAction::none and
    // learn_route_via emits nothing. ⇒ it eliminates a redundant merge, it does not prevent a wrong route. Kept for that
    // reason and for clarity of intent, not as a safety gate. `!d.crypted` is likewise corpus-invisible (0 of 36) but for a
    // different reason worth distinguishing: its 2 entries carry origin 0, which learn_route_via's dest==0 sentinel already
    // refuses — so relying on that would be accidental, whereas this conjunct states the intent. Only `!d.app` is a real
    // barrier (1 of 36: dropping it installs _rt_team[4] on four s28 nodes from a hash-bind payload byte).
    if (for_team_data && !d.app && !d.crypted && ui
        && origin != _active->_pending_rx->from)          // 1 hop is the else-arm above's job (it also fires the triggered beacon)
        learn_route_via(origin, _active->_pending_rx->from, static_cast<uint8_t>(d.committed_hops + 1),
                        protocol::db_to_q4(meta.snr_db), /*team_plane=*/true);
#endif
    _hal.cancel(kPendingRxExpiryTimerId);
    _hal.set_rx_sf(_cfg.routing_sf);                     // receiver retunes back
    _active->_pending_rx.reset();
    // §B153: the per-hop last-acked CACHE THAT WAS WRITTEN HERE IS GONE, along with the RTS-time gate it fed and
    // the two locals (`rx_sf`, `pl`) that existed only to feed it. `_seen_origins` below is now the ONLY dedup —
    // and it is the one holding the evidence (see the argument at handle_rts). Nothing replaces this write.
    const uint64_t nowm = _hal.now();
    // §1b sealed-sender dedup key — TYPE-NAMESPACED into one 64-bit space so PLAINTEXT and CRYPTED can NEVER alias.
    // PLAINTEXT = (origin<<24|dst<<16|ctr), naturally in [0,2^32) — same VALUE as before, just widened (s18 invariant).
    // CRYPTED = the FULL 8-B cleartext nonce-seed loaded LE, top bit forced => [2^63,2^64). The seed is globally unique
    // per message (a crypto invariant), preserved VERBATIM across forwards (so a loop via a different prev-hop still
    // matches), and — once §1c seals `origin` — the ONLY flight-id a relay can read. Forcing bit 63 costs one seed bit
    // (63 left => ~2^-47 birthday at the 256 cap) and makes the plaintext/CRYPTED disjointness a HARD invariant, not a
    // probability. Extract the seed HERE, before the sokey: PLAINTEXT data_nonce_seed() returns an EMPTY span
    // (frame_codec:717), so nseed stays zero and is never read on that path. origin is still read BEFORE the ACK so
    // HOP_BUDGET/LOOP_DUP can NACK instead of re-ACKing.
    uint8_t nseed[8] = {0};
    if (d.crypted) { auto sd = data_nonce_seed(std::span<const uint8_t>(bytes, len), d);
                     for (uint8_t i = 0; i < 8 && i < sd.size(); ++i) nseed[i] = sd[i]; }
    uint64_t seed_u64 = 0; for (int i = 0; i < 8; ++i) seed_u64 |= uint64_t(nseed[i]) << (8 * i);   // LE load (zero for plaintext)
    const uint64_t sokey = d.crypted
        ? (seed_u64 | (uint64_t(1) << 63))                                                          // CRYPTED namespace: >= 2^63
        // PLAINTEXT namespace: < 2^32 for a STATIC (global-id) origin. §mobile: a mobile/team DATA (mobile_from) has a
        // LOCAL-id origin that can §18-collide a static global id -> OR in bit 62 to move it to a DISJOINT plaintext range
        // [2^62, 2^62+2^32) so a team/mobile origin X can never alias a static origin X (a false LOOP_DUP would DROP a real
        // message). mobile_from=0 on s18 -> identical.
        // ★★ §team-parity T6/B (spec §3/T6 Part B): + bit 61 for a TEAM-plane flight, and the reason is a CORRECTION to
        // this slice's own brief AND to the LayerRuntime "§P2-7 AUDIT" note, both of which say this key "has NO plane bit".
        // IT ALREADY HAD ONE — bit 62 above — but bit 62 is `mobile_from`, which is NOT the plane: it is set for a
        // REGISTERED MOBILE's ordinary STATIC-plane DM (whose origin is its home's GLOBAL id) *and* for a team-plane DM
        // (node_mac.cpp:817 ORs it with team_next). So the residual alias was team-vs-mobile-static INSIDE the 2^62 range,
        // not team-vs-static. `for_team_data` (computed at the top of this function, :564 — "this DATA is addressed to OUR
        // team-plane id") is the exact plane discriminator and is stable across every hop of a team flight, since each
        // team hop is addressed addr_len=1 to the next hop's team id. FOUR disjoint ranges now: static <2^32 ·
        // mobile-static [2^62, 2^62+2^32) · TEAM [2^62+2^61, …) · CRYPTED >=2^63.
        // Static reduction: for_team_data is false for every static node and every non-team frame (team_addr_for_us
        // requires team_id!=0 && _team_local_id!=0, and stubs to false on the three MR_FEAT_TEAM 0 gateway_* envs), so
        // no static or mobile-static key VALUE changes — only team-plane flights move, and they move together.
        : (((uint64_t(origin) << 24) | (uint64_t(d.dst) << 16) | d.ctr)
           | (_active->_pending_rx->mobile_from ? (uint64_t(1) << 62) : uint64_t(0))
           | (for_team_data                     ? (uint64_t(1) << 61) : uint64_t(0)));
    // HOP_BUDGET enforcement FIRST (dv:10918-10964), BEFORE the dedup AND the ACK so the
    // NACK fires IN LIEU OF the ACK. A FORWARDER (d.dst != self) decrements the TTL; if the
    // decremented value went negative (the frame arrived with hops_remaining==0 at a
    // non-destination), the budget is exhausted -> NACK the sender (terminal) instead of
    // forwarding. The destination is exempt. Lua runs this check ABOVE the loop-dup dedup,
    // so a budget-exhausted frame ALWAYS HOP_BUDGET-NACKs (terminal + rt-bump self-heal)
    // regardless of dup status.
    const int     hb_new_remaining = static_cast<int>(d.hops_remaining) - 1;
    const uint8_t hb_new_committed = (d.committed_hops >= 7) ? 7
                                     : static_cast<uint8_t>(d.committed_hops + 1);
    if (!for_me_dst(d.dst) && hb_new_remaining < 0) {   // §6.4: the destination (static OR team-plane id) is exempt from the hop-budget NACK
        MR_EMIT("hop_budget_exceeded", EF_I("origin", origin), EF_I("dst", d.dst), EF_I("ctr", d.ctr));
        // Record (origin,dst,ctr) so a LATER non-exhausted arrival of the SAME flight via
        // a DIFFERENT prev-hop is caught as LOOP_DUP (not accepted+forwarded) — dv:10933-10940.
        record_seen_origin(sokey, from, nowm);   // prune + roll-evict-oldest-if-full + insert (see the def)
        nack_in nin{}; nin.reason = protocol::nack_reason_hop_budget; nin.ctr_lo = d.ctr_lo4;
        nin.payload = static_cast<uint8_t>((hb_new_committed & 0x0f) << 4);   // committed in the HIGH nibble
        nin.to = from; nin.mobile_to = _active->_pending_rx->mobile_from;   // §mobile: a mobile/team DATA's origin is a LOCAL id -> mark the NACK
        uint8_t nbuf[4]; const size_t nl = pack_nack(nin, std::span<uint8_t>(nbuf, 4));
        tx_with_retry(nbuf, nl, static_cast<int16_t>(_cfg.routing_sf), FrameTag::nack);   // R4.5b (HOP_BUDGET NACK)
        MR_EMIT("nack_tx", EF_I("to", from), EF_I("reason", protocol::nack_reason_hop_budget), EF_I("ctr", d.ctr));
        become_free();
        return;
    }
    // Origin-level dedup (dv:10966+), AFTER HOP_BUDGET. A same-prev-hop dup is normal
    // lost-ACK recovery (ACK-only below); a DIFFERENT prev-hop means a mesh loop -> NACK.
    auto so = _active->_seen_origins.find(sokey);
    const bool live_dup = (so != _active->_seen_origins.end() && so->second > nowm);
    if (live_dup) {
        auto sof = _active->_seen_origin_from.find(sokey);
        if (sof != _active->_seen_origin_from.end() && sof->second != from) {
            // LOOP_DUP: the SAME flight arrived via a DIFFERENT prev-hop (a mesh loop,
            // dv:10971). NACK the sender so it cascades to an alt, and do NOT ACK (the
            // ACK would clear its pending_tx early). prior_from = the first prev-hop.
            nack_in nin{}; nin.reason = protocol::nack_reason_loop_dup; nin.ctr_lo = d.ctr_lo4;
            nin.payload = sof->second; nin.to = from;
            uint8_t nbuf[4]; const size_t nl = pack_nack(nin, std::span<uint8_t>(nbuf, 4));
            tx_with_retry(nbuf, nl, static_cast<int16_t>(_cfg.routing_sf), FrameTag::nack);   // R4.5b (LOOP_DUP NACK)
            MR_EMIT("nack_tx", EF_I("to", from), EF_I("reason", protocol::nack_reason_loop_dup), EF_I("ctr", d.ctr));
            MR_EMIT("dup_drop", EF_I("origin", origin), EF_I("dst", d.dst), EF_I("ctr", d.ctr));
            become_free();
            return;
        }
    }
    // ACK on routing_sf (2-bit SNR bucket). R4.2: piggyback OUR budget tier, capped at CRITICAL (the
    // protocol caps the forward hint at CRITICAL per Lua dv:11054 — a node already >=CRITICAL refuses
    // the RTS with a BUDGET NACK and rarely ACKs; EXHAUSTED is the reverse-NACK's concern). So the
    // sender learns our congestion in the FORWARD direction. Fires for a fresh DATA and a same-prev-hop dup.
    const BudgetTier my_tier = compute_budget_tier();
    const uint8_t hint = (static_cast<uint8_t>(my_tier) > static_cast<uint8_t>(BudgetTier::critical))
                         ? static_cast<uint8_t>(BudgetTier::critical) : static_cast<uint8_t>(my_tier);
    ack_in ain{}; ain.ctr_lo = d.ctr_lo4; ain.budget_hint = hint;
    ain.snr_bucket = bucket_of_snr_2b(protocol::db_to_q4(meta.snr_db)); ain.to = from;
    // §mobile: if the DM originator is a mobile/team member (its RTS was mobile_src -> _pending_rx->mobile_from), the ACK's
    // `to` is a home-assigned/team LOCAL id -> set mobile_to so the originator ACCEPTS it (its gate at handle_ack requires
    // (mobile_to==1)==is_mobile) and a colliding STATIC id ignores it. Without this EVERY mobile/team-ORIGINATED DM fails at
    // the ACK step (retries + dups). 0 for a static originator -> byte-identical.
    ain.mobile_to = _active->_pending_rx->mobile_from;
    // Inc 3: warn the sender (via the ACK warn bit) when its observed airtime is in the warn band — the
    // soft sender-side precursor to the hard drop. orig_air = this sender's windowed airtime (post-DATA,
    // computed above for the data_rx diagnostic). cap = share x budget; warn at warn_fraction x cap.
    const uint32_t airtime_cap_a = static_cast<uint32_t>(
        static_cast<double>(protocol::originator_airtime_share) * _duty_cycle_budget_ms);
    ain.warn = (_duty_cycle_budget_ms > 0) &&
               (orig_air > static_cast<uint32_t>(
                   static_cast<double>(protocol::originator_airtime_warn_fraction) * airtime_cap_a));
    uint8_t abuf[3]; const size_t al = pack_ack(ain, std::span<uint8_t>(abuf, 3));
    tx_with_retry(abuf, al, static_cast<int16_t>(_cfg.routing_sf), FrameTag::ack);   // R4.5b: stash + tag the ACK
    MR_EMIT("ack_tx", EF_I("to", from), EF_I("ctr", d.ctr), EF_I("airtime_warn", ain.warn ? 1 : 0));
    if (live_dup) { become_free(); return; }                                        // same prev-hop dup -> ACK only
    record_seen_origin(sokey, from, nowm);                                          // record + roll-evict-oldest if full
    // defer deliver/forward by the ACK airtime so it doesn't share a sim step with the ACK.
    _active->_post_ack = PostAck{};
    _active->_post_ack.pending = true; _active->_post_ack.is_forward = !for_me_dst(d.dst);   // §6.4: deliver a DM addressed to our team-plane id too (dual member)
    _active->_post_ack.team_plane = for_team_data;   // ★ §hashbind-plane: carry the PLANE to the deferred ingest (do_post_ack cannot re-derive it — no `next`/`addr_len` there, and _pending_rx is reset above). Same discriminator T6/B already uses for the dedup key; false for every static/non-team frame.
    _active->_post_ack.origin = origin; _active->_post_ack.dst = d.dst; _active->_post_ack.ctr_lo = d.ctr_lo4;
    _active->_post_ack.ctr = d.ctr; _active->_post_ack.flags = d.flags; _active->_post_ack.type = d.type; _active->_post_ack.previous_hop = from;
    _active->_post_ack.inner_len = static_cast<uint8_t>(inner.size() <= protocol::max_payload_bytes_hard_cap
                                               ? inner.size() : protocol::max_payload_bytes_hard_cap);
    for (uint8_t i = 0; i < _active->_post_ack.inner_len; ++i) _active->_post_ack.inner[i] = inner[i];
    if (d.crypted) for (uint8_t i = 0; i < 8; ++i) _active->_post_ack.nonce_seed[i] = nseed[i];   // §1b: seed already extracted above (the dedup key) — stash verbatim for the open / forward
    // Clamp the underflow: the exhaustion NACK that guarantees hb_new_remaining>=0 only fires for a FORWARD
    // (d.dst != self, line above) — the DELIVERY case (d.dst==self) is exempt, so a DM that arrived AT us with
    // hops_remaining==0 leaves hb_new_remaining==-1. That value is dead for a plain deliver, but an L2c
    // misdelivery re-forwards from the delivery case, so -1 -> uint8_t 255 (saturating to the 31 max) must not
    // leak into a budget. (L2c re-budgets its leg from rt anyway; this is belt-and-suspenders.)
    _active->_post_ack.fwd_remaining = static_cast<uint8_t>(hb_new_remaining < 0 ? 0 : hb_new_remaining);
    _active->_post_ack.fwd_committed = hb_new_committed;                         // carried into the forward TxItem
    (void)_hal.after(airtime_routing_ms(3) + 1, kPostAckTimerId);
}

void Node::do_post_ack() {
    if (!_active->_post_ack.pending) return;
    const PostAck pa = _active->_post_ack;
    _active->_post_ack.pending = false;
    if (!pa.is_forward) {
        // Parse the inner up-front (the optional DST_HASH prefix + the cross-layer layer-path, read from pa.flags).
        auto ui = parse_unicast_inner(std::span<const uint8_t>(pa.inner, pa.inner_len), pa.flags);
        // §mobile delegated hash-locate (2026-07-11): a hosted mobile handed us (its home) a PLAINTEXT payload to send to
        // ui->dst_key_hash32 (the target). RE-ORIGINATE via send_by_hash (existing resolve/park machinery), stamping
        // SOURCE_HASH = the requesting mobile's hash (ui->source_hash) so the target's E2E-ack routes back to the MOBILE,
        // not us. VERIFY source_hash is one of OUR mobiles (else the reply couldn't return here + reject a spoof). Checked
        // BEFORE the last-mile fork so a MOBILE_SEND wrapper is never forwarded verbatim. _mobile_reg_n>0 -> non-host inert.
        if (pa.type == DATA_TYPE_MOBILE_SEND && _active->_mobile_reg_n > 0 && ui && ui->has_dst_hash && ui->has_source_hash) {
            bool ours = false;
            for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
                if (_active->_mobile_reg[i].key_hash32 == ui->source_hash) { ours = true; break; }
            if (ours && (pa.flags & DATA_FLAG_MS_ENCLOSED_TYPE) && ui->body.size() >= 2 && ui->body[0] == DATA_TYPE_CHANNEL_POST) {
                // §S7 T-B: a delegated GLOBAL/leaf channel post. Body = [DATA_TYPE_CHANNEL_POST][channel_id][text].
                // Re-originate via do_send_channel under OUR OWN origin/ctr (the home mints; the wrapper's DST_HASH =
                // the mobile's own hash is a placeholder — never used here). Anti-spam bills the HOME + our self-GATE
                // applies (deliberate: hosting implies consenting to the mobile's channel share).
                do_send_channel(ui->body[1], ui->body.data() + 2, static_cast<uint8_t>(ui->body.size() - 2));
                become_free();
                return;
            }
            if (ours && ui->has_cross_layer) {
                // §S1: a CROSS_LAYER delegation. The wrapper carries the DEST path (user hops) + an enclosed-type body
                // prefix. Prepend OUR layer + re-validate fail-loud (== the node.cpp send_layer rules), then originate via
                // the explicit-path machinery stamping SOURCE_HASH = the mobile (so the far recipient's ack routes back
                // here) + the ctr_H->ctr_M map so the ack reaches the mobile with its own ctr (§GapB p2). enclosed_type
                // (0 = plain DM, E2E_ACK = a delegated ack) is threaded through as the re-originated frame TYPE (§1b-4).
                const uint8_t  etype   = ui->body.size() >= 1 ? ui->body[0] : 0;
                const uint8_t* payload = ui->body.size() >= 1 ? ui->body.data() + 1 : nullptr;
                const uint8_t  plen    = ui->body.size() >= 1 ? static_cast<uint8_t>(ui->body.size() - 1) : 0;
                bool valid = ui->n_layers >= 1 && ui->n_layers <= protocol::gw_env_max_hops - 1
                             && ui->layer_ids[0] != active_layer_id();   // 1 + n_layers must fit; hops[0] != our own layer
                for (uint8_t i = 0; valid && i < ui->n_layers; ++i) if (ui->layer_ids[i] == 0) valid = false;
                if (!valid) {
                    MR_EMIT("xl_delegate_bad_path", EF_I("n", ui->n_layers), EF_I("m", static_cast<int64_t>(ui->source_hash)));
                    presence_mark_deleg_fail(ui->source_hash);   // §B2: signal the mobile via the next roster's deleg_fail bit
                } else {
                    uint16_t hctr = 0;
                    const uint8_t reflags = (etype == DATA_TYPE_E2E_ACK) ? 0
                                            : static_cast<uint8_t>(pa.flags & (DATA_FLAG_E2E_ACK_REQ | DATA_FLAG_PRIORITY));
                    const CmdCode code = originate_layer_path(ui->dst_key_hash32, ui->layer_ids, ui->n_layers,
                                                              payload, plen, reflags, hctr,
                                                              /*type=*/etype, /*override_source_hash=*/ui->source_hash);
                    if (code == CmdCode::queued) {
                        if (etype != DATA_TYPE_E2E_ACK && (pa.flags & DATA_FLAG_E2E_ACK_REQ))
                            deleg_ack_put(ui->source_hash, hctr, pa.ctr);   // ctr_H -> ctr_M for the returning far ack
                    } else {
                        MR_EMIT("xl_delegate_no_route", EF_I("m", static_cast<int64_t>(ui->source_hash)), EF_I("code", static_cast<int>(code)));
                        presence_mark_deleg_fail(ui->source_hash);   // §B2: signal the mobile via the next roster's deleg_fail bit
                    }
                }
                become_free();
                return;
            }
            if (ours) {
                // §S2: a SAME-LAYER delegated send. Default = a plain re-origination (byte-identical). If the wrapper
                // set DATA_FLAG_MS_ENCLOSED_TYPE, its body is [enclosed_type:1][payload] (a delegated INTRO): strip the
                // marker + the type byte, and re-originate with that TYPE (send_by_hash type=etype -> no re-attach, the
                // key prefix rode from the mobile). The reply routes back by SOURCE_HASH -> our proxy -> last-mile;
                // mobile_ctr -> the ctr_H->ctr_M reverse-ack map so the target's E2E-ack reaches the mobile with ITS ctr.
                const uint8_t* wb = ui->body.data(); uint8_t wl = static_cast<uint8_t>(ui->body.size()); uint8_t etype = 0;
                uint8_t reflags = pa.flags & (DATA_FLAG_E2E_ACK_REQ | DATA_FLAG_PRIORITY);   // plain path: unchanged (byte-identical)
                if ((pa.flags & DATA_FLAG_MS_ENCLOSED_TYPE) && wl >= 1) {
                    etype = wb[0]; wb += 1; wl = static_cast<uint8_t>(wl - 1);
                    reflags = pa.flags & DATA_FLAG_E2E_ACK_REQ;                              // strip the marker from the re-originated flags
                }
                (void)send_by_hash(ui->dst_key_hash32, wb, wl, reflags, CryptIntent::off,
                                   /*reply_to_hash=*/ui->source_hash, /*mobile_ctr=*/pa.ctr, Plane::AUTO, /*type=*/etype);   // plaintext-only (v1)
            }
            become_free();
            return;
        }
        // §mobile 3a: HOST last-mile forward — a DM addressed to ME whose inner dst_hash is a mobile I HOST -> re-address it
        // to the mobile's LOCAL id with the addr_len=1 mark (Slice 1). The inner rides VERBATIM (E2E-sealed to the mobile;
        // the host re-addresses, never decrypts — like the cross-layer bridge). Gated on _mobile_reg_n>0 -> a non-host is
        // byte-identical. Runs BEFORE the cross-layer/H-answer/deliver forks (the DM's dst_hash != our key routed us here as proxy).
        if (ui && ui->has_dst_hash && ui->dst_key_hash32 != _key_hash32 && _active->_mobile_reg_n > 0) {
            for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i) {
                if (_active->_mobile_reg[i].key_hash32 == ui->dst_key_hash32) {
                    if (_active->_tx_queue_n < kTxQueueCap) {              // best-effort (match the bridge: drop if full)
                        TxItem it{};
                        it.origin     = pa.origin;                        // PRESERVE the real originator (anti-spam)
                        it.dst        = _active->_mobile_reg[i].mobile_local_id;
                        it.addr_len   = 1;                                // §mobile: next is a mobile local-id (Fix 1 -> RTS mark)
                        it.mobile_src = false;                            // a host forward, NOT a mobile origination
                        it.is_forward = true;
                        it.ctr = pa.ctr; it.ctr_lo = pa.ctr_lo;           // carry the flight id
                        it.flags = pa.flags; it.type = pa.type;
                        it.inner_len = pa.inner_len;
                        for (uint8_t j = 0; j < pa.inner_len; ++j) it.inner[j] = pa.inner[j];   // inner VERBATIM (E2E-sealed)
                        // §GapB p2: a DELEGATED-send ack arrives here addressed to the mobile (DST_HASH=M). It carries
                        // ctr_H (the home's re-origination ctr); the mobile awaits ctr_M. Rewrite the 2-B body in place
                        // via the (mobile_hash, ctr_H)->ctr_M map (keying by hash, NOT the acker id — XL ids alias). A
                        // map miss (a DIRECT send, or a plain DM) leaves the bytes verbatim. Covers same-layer AND XL acks.
                        if (pa.type == DATA_TYPE_E2E_ACK && ui->body.size() >= 2) {
                            const uint16_t acked = static_cast<uint16_t>(ui->body[0] | (ui->body[1] << 8));
                            uint16_t m_ctr = acked;
                            if (deleg_ack_translate(ui->dst_key_hash32, acked, m_ctr)) {
                                const size_t boff = static_cast<size_t>(ui->body.data() - pa.inner);
                                if (boff + 1 < sizeof it.inner) {
                                    it.inner[boff]     = static_cast<uint8_t>(m_ctr & 0xFF);
                                    it.inner[boff + 1] = static_cast<uint8_t>(m_ctr >> 8);
                                }
                                MR_EMIT("mobile_reverse_ack", EF_I("local", it.dst), EF_I("ctr", m_ctr));
                            }
                        }
                        _active->_tx_queue[_active->_tx_queue_n++] = it;
                        MR_EMIT("mobile_lastmile_fwd", EF_I("local", it.dst), EF_I("origin", it.origin));
                    }
                    become_free();
                    return;                                              // handled -> do NOT fall into bridge/H-answer/deliver
                }
            }
        }
        // Slice 4c.1 (the bridge KEYSTONE): a CROSS_LAYER DM in TRANSIT through this gateway -> BRIDGE it to the next
        // layer BEFORE any type-based consume (so a 4e cross-layer E2E-ack passing through bridges, not gets consumed).
        // dst_hash == our key => we ARE the recipient: fall through to the normal handling (E2E-ack confirm / deliver to
        // inbox). A malformed CROSS_LAYER inner is REFUSED (drop) — its layer-path bytes must never reach the app as body.
        if (pa.flags & DATA_FLAG_CROSS_LAYER) {
            if (!ui || !ui->has_cross_layer) { become_free(); return; }
            if (!(ui->has_dst_hash && ui->dst_key_hash32 == _key_hash32)) { bridge_cross_layer(pa, *ui); return; }
        }
        if (pa.type == DATA_TYPE_MOBILE_H_ANSWER) {   // §mobile 4a: a mobile-proxy answer -> cache M->home only (NO id_bind, NO deliver)
            on_mobile_hash_bind_response(pa.inner, pa.inner_len);
            become_free();
            return;
        }
        if (pa.type == DATA_TYPE_MOBILE_H_ANSWER_PUBKEY) {   // §mobile Part 2 Fix 8: a home's WANT_PUBKEY answer -> cache peer_key(M) + M->home (NO id_bind, NO deliver)
            on_mobile_hash_bind_pubkey_response(pa.inner, pa.inner_len);
            become_free();
            return;
        }
        if (pa.type == DATA_TYPE_MOBILE_BREADCRUMB) {   // §mobile 4b: a moved mobile's redirect note -> record it against my _mobile_reg[M]
            if (ui && ui->has_source_hash && ui->body.size() >= 3 && _active->_mobile_reg_n > 0)
                for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
                    if (_active->_mobile_reg[i].key_hash32 == ui->source_hash) {   // attribute: only M can move M (SOURCE_HASH)
                        _active->_mobile_reg[i].redirect_home_id    = ui->body[0];
                        _active->_mobile_reg[i].redirect_epoch      = ui->body[1];
                        _active->_mobile_reg[i].redirect_home_layer = ui->body[2];   // §5b: the new home's LAYER
                        MR_EMIT("mobile_redirect_recorded", EF_I("m", i), EF_I("to", ui->body[0]), EF_I("epoch", ui->body[1]));
                        break;
                    }
            become_free(); return;   // consumed (routing info, NOT delivered/inbox'd); no match / non-host -> just drop
        }
        // §S6: DATA_TYPE_MOBILE_PUBKEY_PUSH (TYPE 12) is RETIRED — key custody now rides the presence probe's HAS_PUBKEY
        // block (presence_ingest_probe), confirmed by the roster's has_key bit. No mobile emits TYPE 12 any more; a stray
        // one from an un-upgraded peer falls through to normal DM handling (harmless — the fleet reflashes together).
        if (pa.type == DATA_TYPE_MOBILE_LAYER_QUERY && _cfg.n_layers == 2 && ui && ui->has_source_hash) {   // §mobile 5a: a mobile asks THIS gateway for its bridged layers
            uint8_t body[protocol::max_payload_bytes_hard_cap]; uint8_t off = 1; body[0] = 0;   // [count][records…]
            uint8_t cnt = 0;
            for (uint8_t i = 0; i < _cfg.n_layers; ++i) {
                LayerRecord r{};
                r.layer_id = _cfg.layers[i].layer_id; r.sf = _cfg.layers[i].routing_sf;
                r.freq_khz = protocol::mhz_to_khz(_cfg.layers[i].freq_mhz);
                r.bw_hz = _cfg.layers[i].bw_hz ? _cfg.layers[i].bw_hz : _cfg.radio_bw_hz;
                r.name_len = _cfg.leaf_name_len;
                for (uint8_t k = 0; k < r.name_len && k < protocol::leaf_name_max; ++k) r.name[k] = _cfg.leaf_name[k];
                const size_t n = pack_layer_record(r, std::span<uint8_t>(body + off, sizeof(body) - off));
                if (n == 0) break;
                off = static_cast<uint8_t>(off + n); ++cnt;
            }
            body[0] = cnt;
            // reply to origin (=home_id) with dst_hash=M -> the home last-mile-forwards it to the mobile (reuse the mobile-delivery path)
            (void)enqueue_data(pa.origin, body, off, DATA_FLAG_DST_HASH, "mobile_layer_answer",
                               /*app_dm=*/false, DATA_TYPE_MOBILE_LAYER_ANSWER, CryptIntent::off, /*override_dst_hash=*/ui->source_hash);
            MR_EMIT("mobile_layer_answer_tx", EF_I("to", pa.origin), EF_I("count", cnt));
            become_free(); return;
        }
#if MR_FEAT_MOBILE
        if (pa.type == DATA_TYPE_MOBILE_LAYER_ANSWER && _cfg.is_mobile) {   // §mobile 5a: the mobile ingests the learned layer directory
            if (ui) learned_layers_ingest(ui->body.data(), ui->body.size());
            become_free(); return;
        }
        if (pa.type == DATA_TYPE_MOBILE_KEY_FORWARD && _cfg.is_mobile) {   // §S3 part2: the home forwarded a reqpubkey requester's key -> cache it (closes the recipient-side decrypt gap)
            if (ui) on_mobile_key_forward(ui->body.data(), static_cast<uint8_t>(ui->body.size()));
            become_free(); return;
        }
#endif
        if (pa.type == DATA_TYPE_H_ANSWER || pa.type == DATA_TYPE_AUTHORITATIVE_H_ANSWER) {   // a hash-bind answer for us -> consume (routing info, NOT a DM)
            on_hash_bind_response(pa.inner, pa.inner_len, pa.type == DATA_TYPE_AUTHORITATIVE_H_ANSWER, pa.team_plane);   // ★ §hashbind-plane: a TEAM-plane answer must not write the static _id_bind
            become_free();
            return;
        }
        if (pa.type == DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY) {   // E2E §6: the owner's pubkey answer -> cache (routing/key info, NOT a DM)
            if (ui) on_hash_bind_pubkey(ui->body.data(), static_cast<uint8_t>(ui->body.size()));   // Wave 2: TYPE 5 is now a standard DM -> the pubkey is the BODY (past [dst_hash?][origin])
            become_free();
            return;
        }
        if (pa.type == DATA_TYPE_REMOTE_CMD || pa.type == DATA_TYPE_REMOTE_RESP) {   // OTA remote diagnostics: STAGE for the main loop
            // NOT inbox'd / delivered-as-message, NOT consumed-silently like an E2E ack — fw_main executes (cmd) or prints
            // (resp) on the main loop, never the RX path. One in flight; a 2nd while pending drops (rcmd is human-paced).
            if (_remote_inbound.active) {
                MR_EMIT("remote_inbound_drop_full", EF_I("from", pa.origin));
            } else {
                const uint8_t* src = ui ? ui->body.data() : ((pa.inner_len > 1) ? pa.inner + 1 : nullptr);   // inner = [origin][body…]; body is ui->body (cleartext)
                uint8_t n = ui ? static_cast<uint8_t>(ui->body.size()) : ((pa.inner_len > 1) ? static_cast<uint8_t>(pa.inner_len - 1) : 0);
                if (n > protocol::inbox_max_body) n = protocol::inbox_max_body;
                _remote_inbound.active      = true;
                _remote_inbound.is_response = (pa.type == DATA_TYPE_REMOTE_RESP);
                _remote_inbound.from        = pa.origin;
                _remote_inbound.len         = n;
                for (uint8_t i = 0; i < n; ++i) _remote_inbound.body[i] = src ? src[i] : 0;
            }
            become_free();
            return;
        }
        if (pa.type == DATA_TYPE_E2E_ACK) {              // an end-to-end ACK for a DM we originated -> confirm + RECORD a receipt, not deliver
            // The acked ctr from the parsed inner BODY — uniform across all ack shapes now (§GapB): whatever optional
            // fields precede it (DST_HASH / CROSS_LAYER path / SOURCE_HASH), parse_unicast_inner lands ui->body on the
            // 2-B ctr. A raw fallback (no parse) reads [origin][ctr_lo][ctr_hi]. Same value on the plain path -> s18-safe.
            const uint16_t acked = (ui && ui->body.size() >= 2)
                                   ? static_cast<uint16_t>(ui->body[0] | (ui->body[1] << 8))
                                   : ((pa.inner_len >= 3) ? static_cast<uint16_t>(pa.inner[1] | (pa.inner[2] << 8)) : 0);
            // §GapB: the same-layer reverse-ack fork (SOURCE_HASH-marked) is RETIRED. A delegated-recipient ack now
            // arrives DST_HASH-addressed and is last-miled + ctr-rewritten by the generic hosted-mobile fork (:699),
            // BEFORE reaching here. So an ack that reaches HERE is genuinely for one of OUR own sends -> record + push.
            // Cross-layer: the acker's STABLE key (the 8-bit origin aliases across leaves) -> the companion's match key.
            // Same-layer: (origin, ctr) suffices, acker_hash=0.
            const uint32_t acker_hash = ((pa.flags & DATA_FLAG_CROSS_LAYER) && ui && ui->has_source_hash) ? ui->source_hash : 0;
            _inbox.record_ack(pa.origin, acked, active_layer_id(), _hal.now(), acker_hash);   // durable receipt (DM store); inert if no backend (sim)
            Push pu{}; pu.kind = PushKind::send_e2e_acked; pu.dst = pa.origin; pu.ctr = acked; pu.sender_hash = acker_hash; enqueue_push(pu);   // live fast-path (E2E-ACKED ctr=X from=D); sender_hash = the acker's stable key (XL) so the app matches (sender_hash,ctr)
            e2e_ack_clear(pa.origin, acked, acker_hash);   // ★ shelf item (i): CLEAR the pending-ack deadline (emit-free) — mirrors the {dst,ctr,sender_hash} just pushed. A LATE ack (past the deadline) finds nothing -> no-op.
            MR_EMIT("e2e_ack_rx", EF_I("from", pa.origin), EF_I("ctr", acked));  // KEEP for the sim analyzer (free on metal)
            become_free();
            return;
        }
        // L2c verify-on-delivery (NON-cross-layer; cross-layer was forked above): DST_HASH present and naming a key
        // that ISN'T ours => an id collision misdelivered this DM. Heal the collision + redirect to the real owner.
        if (ui && ui->has_dst_hash && ui->dst_key_hash32 != _key_hash32) {
            l2c_handle_misdelivery(pa, ui->dst_key_hash32);     // forward to the real owner (identity-preserving)
            return;                                             // l2c re-kicks the queue itself (become_free)
        }
        // §S2 INTRO (type 15): a PLAINTEXT first-contact DM addressed to US, body = [ed_pub 32][name_len][name][text].
        // Verify ed_pub[:4] == SOURCE_HASH (the peerkey self-consistency rule), cache the sender's key AUTHORITATIVE +
        // name (fires peer_key_cached), STRIP the prefix, and fall through to the NORMAL deliver (enc absent; dedup
        // (sender_hash,ctr) unchanged; the INTRO framing is transport detail). Malformed/inconsistent/short -> DROP
        // loud (telemetry), never deliver raw key bytes. NOT crypto-gated on the receive side: a node WITHOUT an
        // identity can still cache a peer's key (peer_key_set) — but s18 never receives one (nobody attaches, §S2 gate).
        if (pa.type == DATA_TYPE_INTRO) {
            if (!ui || !ui->has_source_hash || ui->body.size() < 33) {                          // needs SOURCE_HASH + at least [ed_pub 32][name_len]
                MR_EMIT("intro_reject", EF_I("reason", 1), EF_I("ctr", pa.ctr)); become_free(); return;
            }
            const uint8_t* ed = ui->body.data();
            const uint32_t ed_hash = key_hash32_of(ed);   // §P2-6: identity.h owns the LE(ed_pub[:4]) derivation
            if (ed_hash != ui->source_hash) {                                                    // self-consistency: the attached key MUST hash to the claimed sender
                MR_EMIT("intro_reject", EF_I("reason", 2), EF_I("hash", static_cast<int64_t>(ui->source_hash))); become_free(); return;
            }
            uint8_t nlen = ui->body[32]; if (nlen > 32) nlen = 32;
            if (static_cast<size_t>(33) + nlen > ui->body.size()) {                              // the name field overruns the frame
                MR_EMIT("intro_reject", EF_I("reason", 3), EF_I("nlen", nlen)); become_free(); return;
            }
            const char* nm = nlen ? reinterpret_cast<const char*>(ui->body.data() + 33) : nullptr;
            if (!peer_key_set(ui->source_hash, ed, PeerKeyConf::authoritative, nm, nlen)) {       // (re-derives + re-checks ed_pub[:4]==hash)
                MR_EMIT("intro_reject", EF_I("reason", 4), EF_I("hash", static_cast<int64_t>(ui->source_hash))); become_free(); return;
            }
            MR_EMIT("peer_key_cached", EF_I("hash", static_cast<int64_t>(ui->source_hash)), EF_I("node", pa.origin));
            push_peer_key_cached(ui->source_hash);                                               // §7: the app's "secure send ready" (+ the cached name)
            ui->body = ui->body.subspan(static_cast<size_t>(33) + nlen);                         // STRIP -> deliver the remaining message as a plain DM (fall through)
        }
        // E2E OPEN (§1a sealed-sender): a CRYPTED DM carries NO cleartext sender hint -> TRIAL DECRYPT over the cached
        // peer keys; the Poly1305 tag identifies the sender + opens the sealed {source_hash + location + body}. The seed
        // rides the trailer. FAIL LOUD: no cached key opens it -> SILENT DROP (never deliver ciphertext to the app).
        uint32_t dec_source_hash = 0; bool dec_has_loc = false; int32_t dec_lat = 0, dec_lon = 0;
        // ADDENDUM 4 (2026-06-25): static, NOT stack. do_post_ack is non-reentrant (one timer fires at a time in the
        // single loop task) and is the deepest path on the cramped 4 KB FreeRTOS loop stack; the DWT watchpoint caught
        // route_strictly_better (reached via send_e2e_ack, below) overflowing right here. Moving these ~480 B of
        // payload buffers off the frame restores the headroom at the exact overflow point. e2e_open_trial fills
        // dec_body before any read; dec_body_len stays a fresh local (re-set each call).
        static uint8_t dec_body[protocol::max_payload_bytes_hard_cap]; uint8_t dec_body_len = 0;
        uint32_t dec_origin = pa.origin;   // §1a: for CRYPTED the trial recovers origin (== cleartext now; from the seal at 1c)
        bool crypted_ok = false;
        // §S4 SEALED_RELAY (type 17): a PLAINTEXT-framed DM whose BODY = [seal_ctr 2][seed8 8][ct‖tag], sealed to US by
        // the CLEAR SOURCE_HASH (a mobile delegating via its home, or a static crossing a layer). DIRECTED open (no trial:
        // the sender is named in the clear); e2e_open_relay verifies the SEALED source_hash == the clear one (anti-spoof)
        // and IGNORES the sealed origin byte (§1c layer-local garbage). Success -> crypted_ok + fall into the shared
        // deliver as an ENCRYPTED DM (enc=1). Fail/short/spoof -> SILENT DROP (never deliver ciphertext). NOT gated on
        // _crypto_ready in the wire dispatch, but e2e_open_relay refuses without an identity -> s18-inert (no seals).
        if (pa.type == DATA_TYPE_SEALED_RELAY) {
            if (!ui || !ui->has_source_hash || !e2e_open_relay(ui->body.data(), ui->body.size(), ui->source_hash, dec_body, dec_body_len)) {
                MR_EMIT("e2e_open_no_key", EF_I("ctr", pa.ctr), EF_I("relay", 1));   // no key / tag fail / spoof -> DROP
                become_free(); return;
            }
            crypted_ok = true; dec_source_hash = ui->source_hash;   // the CLEAR (and seal-verified) sender is the identity
        }
        if (pa.flags & DATA_FLAG_CRYPTED) {
            // §1a sealed-sender: no cleartext sender hint -> TRIAL DECRYPTION over the cached keys; the tag identifies it.
            uint32_t trial_sender = 0;
            if (!e2e_open_trial(pa.inner, pa.inner_len, pa.nonce_seed, pa.flags, pa.ctr, trial_sender, dec_origin,
                                dec_source_hash, dec_has_loc, dec_lat, dec_lon, dec_body, dec_body_len)) {
                MR_EMIT("e2e_open_no_key", EF_I("ctr", pa.ctr));            // no cached key opens it -> SILENT DROP (no push/ack/inbox)
                become_free(); return;
            }
            crypted_ok = true; (void)trial_sender;   // dec_source_hash (sealed, anti-spoof-verified) == trial_sender = the sender
        }
        // §team-ch-key T-K3 (type 19): a teammate GRANTED us the team CONTENT keypair. Placed HERE — after BOTH open
        // paths, before the shared deliver — because the body is SEALED, so nothing above this point can read it.
        // ★ SEALED-ONLY IS ENFORCED ON RECEIPT TOO, not just at origination: a plaintext grant is either a bug in a
        // peer or an attacker feeding us a key we would then encrypt the team's traffic under, so refuse it loudly and
        // NEVER fall through (the body would otherwise be delivered as inbox text = 37 raw key bytes in the app).
        // CONSUMED unconditionally — every outcome, adopt or refusal, returns here: no inbox record, no msg_recv push,
        // no E2E ack (v1 carries no ack request on a grant; the receiver's team_key_received push is the app-level
        // confirmation and a re-grant is idempotent). Ungated by MR_FEAT_TEAM on purpose: on a MR_FEAT_TEAM 0 build
        // team_key_grant_receive answers no_team by construction, which is still a loud consume rather than a delivery.
        if (pa.type == DATA_TYPE_TEAM_KEY_GRANT) {
            if (!crypted_ok) {
                MR_EMIT("team_key_grant_reject", EF_I("reason", static_cast<int>(TeamKeyGrantRx::not_sealed)),
                        EF_I("from", pa.origin), EF_I("ctr", pa.ctr), EF_I("len", pa.inner_len));
                become_free(); return;
            }
            (void)team_key_grant_receive(dec_body, dec_body_len, dec_source_hash, pa.origin);   // emits/pushes its own outcome
            become_free(); return;
        }
        // deliver: body from the parsed inner (raw inner[1..] fallback — origin at inner[0] — if it didn't parse).
        static char body[protocol::max_payload_bytes_hard_cap + 1];   // ADDENDUM 4: static (non-reentrant) — paired with dec_body, off the do_post_ack stack frame
        uint8_t blen;
        if (crypted_ok) { blen = dec_body_len;                              // the DECRYPTED body (sealed region opened above)
                          for (uint8_t i = 0; i < blen; ++i) body[i] = static_cast<char>(dec_body[i]); }
        else if (ui)    { blen = static_cast<uint8_t>(ui->body.size());
                          for (uint8_t i = 0; i < blen; ++i) body[i] = static_cast<char>(ui->body[i]); }
        else            { blen = (pa.inner_len > 1) ? static_cast<uint8_t>(pa.inner_len - 1) : 0;
                          for (uint8_t i = 0; i < blen; ++i) body[i] = static_cast<char>(pa.inner[1 + i]); }
        body[blen] = '\0';
        MR_EMIT("delivered", EF_I("origin", dec_origin), EF_I("dst", pa.dst), EF_I("ctr", pa.ctr), EF_S("payload", body));  // dm_delivery keys (dst, payload)
        // sender_hash = the origin's stable key_hash32 (when SOURCE_HASH was set) — the app's DM dedup identity.
        const uint32_t sender_hash = crypted_ok ? dec_source_hash : ((ui && ui->has_source_hash) ? ui->source_hash : 0);
        // Record-on-delivery FIRST (the FINAL-destination deliver path, once per delivered DM): it returns the
        // inbox seq (0 if disabled). The live msg_recv push then carries the SAME sender_hash + seq as the pulled
        // record -> the app dedups by (sender_hash, ctr) and detects a dropped live push by the seq (model B).
        const uint8_t rx_layer = active_layer_id();   // §2/Q13: which layer this DM arrived on (disambiguates origin on a gateway)
        // §GapA: the SENDER's layer (the preserved XL path's first entry) so the recipient can build the (layer_path, hash)
        // REPLY address. 0 for a same-layer / non-XL DM. Computed BEFORE record so the durable record carries it too (§GapA-durable).
        const uint8_t origin_layer = (ui && ui->has_cross_layer && ui->n_layers >= 1) ? ui->layer_ids[0] : 0;
        const uint32_t seq = _inbox.record_dm(dec_origin, sender_hash, pa.ctr, rx_layer,
                                              reinterpret_cast<const uint8_t*>(body), blen, _hal.now(), /*enc=*/crypted_ok ? 1 : 0, origin_layer);  // §8b + §GapA-durable
        Push pu{}; pu.kind = PushKind::msg_recv; pu.origin = dec_origin; pu.dst = pa.dst; pu.ctr = pa.ctr;   // §1a: recovered origin for CRYPTED
        pu.layer_id = rx_layer; pu.sender_hash = sender_hash; pu.seq = seq; pu.enc = crypted_ok;   // §8b: was this DM sealed?
        pu.origin_layer = origin_layer;   // 0 -> the JSON omits it (byte-identical)
        pu.body_len = blen; for (uint8_t i = 0; i < blen; ++i) pu.body[i] = static_cast<uint8_t>(body[i]);
        // LOCATION (spec §5): the sender piggybacked its 6-B location -> surface it to the app on the Push (always
        // compiled — the companion renders it) + a peer_location telemetry for the sim/gate (device-stripped).
        // ★★ §AB4 (2026-07-31): the "firmware-side peer-location cache" this comment used to call an optional follow-up
        // IS NOW BUILT — the _peer_loc retention below. Nothing about the extraction changed: this block already parsed
        // the position and already distinguished sealed from plaintext, so the whole slice is the one peer_loc_set call
        // (U1 — no second extraction, no second sealed/plaintext test, no second emit).
        const bool    loc_present = crypted_ok ? dec_has_loc : (ui && ui->has_location);
        const int32_t loc_lat     = crypted_ok ? dec_lat : (ui ? ui->lat_e7 : 0);
        const int32_t loc_lon     = crypted_ok ? dec_lon : (ui ? ui->lon_e7 : 0);
        if (loc_present) {
            pu.has_location = true; pu.lat_e7 = loc_lat; pu.lon_e7 = loc_lon;
            MR_EMIT("peer_location", EF_I("origin", dec_origin), EF_I("hash", static_cast<int64_t>(sender_hash)), EF_I("lat_e7", loc_lat),
                    EF_I("lon_e7", loc_lon));
            // ★★★ §AB4 RETENTION (address-book spec §2.7). `sender_hash` is already in scope right above and is exactly
            // the key the book needs (§1.2: ids are addresses, the hash is the identity).
            // ★★ AUTHENTICATED ONLY — the C2 gate, and the reason it is HERE rather than inside peer_loc_set: this is
            // where the evidence lives. `crypted_ok` means the inner OPENED with OUR key, so the position is anchored
            // PAIRWISE ⇒ PeerLocSrc::peer, "this specific peer said so". The send side makes that airtight in the other
            // direction: node_mac.cpp REFUSES a `-l` DM that would not be sealed (`unsealable`), so our own firmware can
            // never air a plaintext position at all.
            // ⇒ a plaintext DATA_FLAG_LOCATION (an older or foreign node, or a spoof) is parsed and pushed EXACTLY as
            // before but NEVER retained, because an unauthenticated position is spoofable by anyone in range and a
            // spoofed position in an address book is worse than an absent one — the UI presents it as fact.
            // `sender_hash == 0` is folded into the same refusal: with no identity there is nothing to key it by, so it
            // is unattributable for the same reason even if the frame was sealed.
            // ★ O6 (owner-ruled): the refusal EMITS rather than dropping silently — visibility with no behaviour change,
            // and a spoof attempt becomes observable. `enc` distinguishes "not sealed" from "sealed but hashless".
            if (crypted_ok && sender_hash) {
                (void)peer_loc_set(sender_hash, loc_lat, loc_lon, PeerLocSrc::peer);   // false only for hash 0, excluded above
            } else {
                MR_EMIT("peer_location_unauth", EF_I("origin", dec_origin), EF_I("hash", static_cast<int64_t>(sender_hash)),
                        EF_I("lat_e7", loc_lat), EF_I("lon_e7", loc_lon), EF_I("enc", crypted_ok ? 1 : 0));
            }
        }
        enqueue_push(pu);                                // app channel: the inbound message (live notify, seq-stamped)
        // E2E ACK requested -> reply with the acked ctr. §GapB: CROSS_LAYER -> a NORMAL send on the reversed path
        // (send_xl_ack: STATIC recipient originates, MOBILE recipient delegates via its home); else the same-layer ack.
        if (pa.flags & DATA_FLAG_E2E_ACK_REQ) {
            if ((pa.flags & DATA_FLAG_CROSS_LAYER) && ui) send_xl_ack(*ui, pa.ctr);
            else                                          send_e2e_ack(dec_origin, pa.ctr, sender_hash);   // §1a: ack the recovered origin; §mobile: sender_hash a hosted mobile -> last-mile the ack (origin==my id => self-send)
        }
        become_free();
    } else {
        // §intra-layer-relay (2026-07-05): a GATEWAY does NOT relay other nodes' same-leaf traffic by default (design §6).
        // A cross-layer transit DM is addressed TO the gateway (dst==_node_id -> the deliver/BRIDGE branch above), so ANY
        // forward (dst!=_node_id) reaching HERE on a gateway is an intra-leaf relay -> DROP (unless the operator opted in).
        // The cross-layer bridge (drain_xl_handoffs re-inject) is a SEPARATE originated-TX path, NOT this received-DATA
        // forward, so it is unaffected. Belt-and-suspenders to the sender-side next_hop_selectable gate (Edit 3).
        if (_cfg.is_gateway && !_cfg.intra_layer_relay) {
            MR_EMIT("gateway_intra_relay_drop", EF_I("dst", pa.dst), EF_I("origin", pa.origin));
            become_free();
            return;
        }
        // C.2 cache-on-pass: a relayed hash-bind answer is cleartext -> snoop the binding before forwarding.
        if (pa.type == DATA_TYPE_H_ANSWER || pa.type == DATA_TYPE_AUTHORITATIVE_H_ANSWER)
            on_hash_bind_snoop(pa.inner, pa.inner_len, pa.type == DATA_TYPE_AUTHORITATIVE_H_ANSWER, pa.team_plane);   // ★ §hashbind-plane: cache-on-pass of a TEAM-plane answer must not write the static _id_bind
        else if (pa.type == DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY) {   // E2E §6: cache-on-pass — no `ui` on the forward path, so skip [dst_hash?][origin] to reach the pubkey BODY (Wave 2 standard DM; no SOURCE_HASH on this app_dm=false type)
            const uint8_t off = static_cast<uint8_t>((pa.flags & DATA_FLAG_DST_HASH ? 4 : 0) + 1);
            if (pa.inner_len > off) on_hash_bind_pubkey(pa.inner + off, static_cast<uint8_t>(pa.inner_len - off));
        }
        TxItem it{};
        it.origin = pa.origin; it.dst = pa.dst; it.ctr = pa.ctr; it.ctr_lo = pa.ctr_lo;
        it.flags = pa.flags; it.type = pa.type; it.is_forward = true; it.previous_hop = pa.previous_hop;
        it.inner_len = pa.inner_len;
        for (uint8_t i = 0; i < pa.inner_len; ++i) it.inner[i] = pa.inner[i];
        for (int i = 0; i < 8; ++i) it.nonce_seed[i] = pa.nonce_seed[i];   // CRYPTED: a relay re-tx's the original nonce-seed verbatim
        it.fwd_remaining = pa.fwd_remaining; it.fwd_committed = pa.fwd_committed;   // carry the decremented budget
        it.enqueue_time_ms = _hal.now();                 // fresh hop attempt (dv:11391): the cascade-requeue
                                                         // total-age window starts when THIS hop accepts the
                                                         // forward — else it defaults 0 and the cap mis-fires.
        if (_active->_tx_queue_n < kTxQueueCap) _active->_tx_queue[_active->_tx_queue_n++] = it;
        become_free();
    }
}

// ---- Slice 4c.1: cross-layer DM bridge (the keystone) ------------------------------------------------------------
// Resolve key_hash32 -> node_id on a SPECIFIC leaf's id_bind. NEVER via _active-> : the bridge writes a NON-active
// leaf, and any _active-> deref mid-resolve would read/corrupt the wrong leaf's state (the subtlest aliasing trap).
// Mirrors id_bind_find_by_hash's match (key + not-expired, self exempt) but on _layers[leaf]. -1 = unknown.
int Node::id_on_leaf_by_hash(uint8_t leaf, uint32_t key_hash32) const {
    if (leaf >= _n_layers) return -1;
    const LayerRuntime& L = _layers[leaf];
    const uint64_t now = _hal.now();
    for (uint16_t i = 0; i < L._id_bind_n; ++i) {
        if (L._id_bind[i].key_hash32 != key_hash32) continue;
        const bool self_keep = (L._id_bind[i].node_id == _cfg.layers[leaf].node_id && L._id_bind[i].key_hash32 == _key_hash32);
        if (!self_keep && _cfg.id_bind_ttl_ms > 0
            && (now - L._id_bind[i].last_seen_ms) >= _cfg.id_bind_ttl_ms) continue;   // expired -> skip
        return L._id_bind[i].node_id;
    }
    return -1;
}

// Loop suppression: seed the TARGET leaf's _seen_origins for the re-inject's (origin, dst, ctr) so when THIS gateway
// later hears its own relay on that leaf it is caught as a live_dup (ACK-only, no re-bridge / re-forward). Mirrors
// record_seen_origin but on _layers[leaf] (the bridge writes a non-active leaf).
void Node::seed_seen_origin_on_leaf(uint8_t leaf, uint8_t origin, uint8_t dst, uint16_t ctr) {
    if (leaf >= _n_layers) return;
    // PLAINTEXT-namespace key (< 2^32), matching handle_data's non-CRYPTED sokey. (A CRYPTED DM never reaches a gateway
    // bridge — e2e_dm + cross-layer is refused — so this path is always plaintext.)
    const uint64_t sokey = (static_cast<uint64_t>(origin) << 24) | (static_cast<uint64_t>(dst) << 16) | ctr;
    const uint64_t now = _hal.now();
    _layers[leaf]._seen_origins[sokey]     = now + protocol::seen_origin_ttl_ms;
    _layers[leaf]._seen_origin_from[sokey] = _node_id;   // we re-injected it
}

// Buffer a handoff into the node-global ring; false = full (the caller REFUSES loud — never drop-oldest a transit DM).
bool Node::push_xl_handoff(const XlHandoff& h) {
    for (uint8_t i = 0; i < protocol::cap_gateway_handoffs; ++i)
        if (!_xl_handoffs[i].valid) { _xl_handoffs[i] = h; return true; }
    return false;
}

void Node::bridge_cross_layer(const PostAck& pa, const data_unicast_inner& ui) {
    // L13 (2026-07-04): a SINGLE-layer node NEVER bridges — only a dual-layer gateway (n_layers==2) does. Without
    // this guard a crafted CROSS_LAYER DM whose target_layer_id == our own single leaf's layer_id would match the
    // loop below (target_leaf=0), fill the cap-1 handoff slot, and induce an H-flood for up to ~60 s (a cheap DoS).
    // Refuse at the top (the caller relies on us to become_free()+return, matching the no-leaf-match early-refuse).
    if (_n_layers < 2) { become_free(); return; }
    // ui.has_cross_layer is guaranteed by the caller. The next layer to ENTER = layer_ids[cur].
    const uint8_t target_layer_id = ui.layer_ids[ui.cur];
    // Which of OUR leaves carries that layer_id? (A gateway owns 2.) Not one of ours -> REFUSE loud (no default leaf).
    // §xl-nibble-match (2026-07-05, metal): match by the LEAF NIBBLE, not the full 8-bit id. A single-layer originator
    // reports active_layer_id() == leaf_id (the NIBBLE, since layers[0].layer_id = leaf_id when n_layers==1), so a
    // reversed 4e path can carry a nibble (e.g. 4) where the gateway holds the full id (100). The nibble is the canonical
    // wire identity; validate_gateway_layers (node.cpp) guarantees DISTINCT nibbles, so it's unambiguous — and it aligns
    // with select_gateway_for_leaf + the 4e's own `rev[1] & 0x0F`.
    int target_leaf = -1;
    for (uint8_t i = 0; i < _n_layers; ++i) if ((_cfg.layers[i].layer_id & 0x0F) == (target_layer_id & 0x0F)) { target_leaf = i; break; }
    if (target_leaf < 0) {
        MR_EMIT("xl_bridge_refused", EF_I("reason", 1), EF_I("target_layer", target_layer_id), EF_I("origin", pa.origin), EF_I("ctr", pa.ctr));
        become_free(); return;
    }
    // The stable recipient identity (dst_hash) must be present + resolvable on the TARGET leaf. Unknown binding ->
    // Slice 4f defers (H-flood + handoff TTL); v1 (4c.1) REFUSES loud (drop, NEVER a silent reroute).
    if (!ui.has_dst_hash) {
        MR_EMIT("xl_bridge_refused", EF_I("reason", 2), EF_I("origin", pa.origin), EF_I("ctr", pa.ctr));
        become_free(); return;
    }
    int dst_node = id_on_leaf_by_hash(static_cast<uint8_t>(target_leaf), ui.dst_key_hash32);   // -1 = unknown -> 4f DEFERS (resolve at drain + H-flood), never drops
    if (dst_node < 0) {                                          // §5b: a MOBILE? resolve to its home on the target leaf (the home last-mile-forwards; inner dst_hash=M rides intact)
        const int mhome = mobile_home_on_leaf(static_cast<uint8_t>(target_leaf), ui.dst_key_hash32);
        if (mhome > 0) dst_node = mhome;
    }
    // Advance cur ONLY if a further gateway hop remains (multi-gateway, reserved). v1: cur == n_layers-1 -> unchanged.
    uint8_t new_cur = ui.cur;
    if (static_cast<uint8_t>(ui.cur + 1) < ui.n_layers) new_cur = static_cast<uint8_t>(ui.cur + 1);
    // The re-inject inner is the ORIGINAL preserved verbatim (dst_hash + the full layer-path + origin + source_hash +
    // body); only the cursor byte is patched for a multi-gw advance. dst_node 0 = UNRESOLVED (drain re-resolves + H-floods).
    XlHandoff h{};
    h.valid = true; h.target_leaf = static_cast<uint8_t>(target_leaf);
    h.dst_node_id = (dst_node > 0) ? static_cast<uint8_t>(dst_node) : 0;
    h.dst_key_hash32 = ui.dst_key_hash32;                     // 4f: re-resolve + H-flood the binding on the target leaf
    h.origin = pa.origin; h.ctr = pa.ctr; h.ctr_lo = pa.ctr_lo; h.flags = pa.flags; h.type = pa.type;
    for (int i = 0; i < 8; ++i) h.nonce_seed[i] = pa.nonce_seed[i];   // S1: CRYPTED transit DM keeps the originator's seed across the bridge
    h.inner_len = pa.inner_len;
    for (uint8_t i = 0; i < pa.inner_len; ++i) h.inner[i] = pa.inner[i];
    if (new_cur != ui.cur) {                                   // patch cur (layer-path offset = dst_hash?4:0; cur byte at off+1)
        const uint8_t off = static_cast<uint8_t>(ui.has_dst_hash ? 4 : 0);
        if (static_cast<size_t>(off) + 1 < h.inner_len) h.inner[off + 1] = new_cur;
    }
    h.queued_at_ms = _hal.now();
    if (!push_xl_handoff(h)) {                                 // full -> REFUSE loud (the sender's E2E ack just won't come)
        MR_EMIT("xl_handoff_full", EF_I("origin", pa.origin), EF_I("dst", dst_node), EF_I("ctr", pa.ctr));
        become_free(); return;
    }
    // (The loop-suppression seed moves to the DRAIN: for an UNRESOLVED handoff dst_node isn't known yet; the drain
    //  seeds _seen_origins right before building the re-inject, once the recipient id is resolved on the target leaf.)
    MR_EMIT("xl_bridge", EF_I("origin", pa.origin), EF_I("dst", dst_node), EF_I("ctr", pa.ctr),
            EF_I("target_leaf", target_leaf), EF_I("cur", new_cur), EF_I("resolved", dst_node > 0 ? 1 : 0));
    become_free();                                            // drains (resolved) or defers + H-floods (unresolved) on the target leaf's window
}

// Called from activate_layer(leaf) with _active == &_layers[leaf]: move every handoff targeting this leaf into its
// tx_queue as a fresh-budget RELAY leg (identity preserved — mirrors l2c_enqueue_forward; + is_gw_relay + the DM type).
void Node::drain_xl_handoffs_for_leaf(uint8_t leaf) {
    if (leaf >= _n_layers || &_layers[leaf] != _active) return;   // the drain is _active-COUPLED (re-resolve/flood/rt/enqueue use _active) — refuse a wrong-leaf call
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < protocol::cap_gateway_handoffs; ++i) {
        XlHandoff& h = _xl_handoffs[i];
        if (!h.valid || h.target_leaf != leaf) continue;
        // Slice 4f: an UNRESOLVED handoff (binding unknown at bridge) re-resolves on THIS (now-active target) leaf's
        // id_bind. Found -> drain below. Still unknown -> H-flood the binding on this leaf (throttled, one per visit)
        // + keep deferred; on the TTL -> give up LOUD (X's DM retry recovers it — an ack/transit DM never floods home).
        if (h.dst_node_id == 0) {
            const int rid = id_bind_find_by_hash(h.dst_key_hash32);   // _active == &_layers[leaf] here
            const int mhome = (rid > 0) ? -1 : mobile_home_find(h.dst_key_hash32);   // §5b: a mobile? resolve to its home on THIS (target) leaf
            if (rid > 0) {
                h.dst_node_id = static_cast<uint8_t>(rid);
            } else if (mhome > 0) {                                  // §5b: the mobile's home on this leaf -> deliver there (home last-mile-forwards; inner dst_hash=M intact)
                h.dst_node_id = static_cast<uint8_t>(mhome);
                MR_EMIT("xl_mobile_resolved", EF_I("home", mhome), EF_I("ctr", h.ctr), EF_I("leaf", leaf));
            } else if (now - h.queued_at_ms >= protocol::gateway_handoff_defer_ttl_ms) {
                MR_EMIT("xl_handoff_giveup", EF_I("origin", h.origin), EF_I("ctr", h.ctr), EF_I("dst_hash", static_cast<int64_t>(h.dst_key_hash32)), EF_I("leaf", leaf));
                h.valid = false;                                     // TTL exceeded -> DROP loud
                continue;
            } else {
                if (h.last_h_flood_ms == 0 || now - h.last_h_flood_ms >= protocol::gateway_handoff_reflood_ms) {  // 0 = never flooded -> fire now
                    emit_hash_query(h.dst_key_hash32, /*hard=*/false);   // flood an H query on THIS leaf (we're on it now)
                    // ONE H query resolves the binding for EVERY pending handoff to this hash -> stamp them all so
                    // siblings don't re-flood the SAME query this pass / window (review #4: no duplicate floods).
                    for (uint8_t j = 0; j < protocol::cap_gateway_handoffs; ++j)
                        if (_xl_handoffs[j].valid && _xl_handoffs[j].dst_node_id == 0 && _xl_handoffs[j].dst_key_hash32 == h.dst_key_hash32)
                            _xl_handoffs[j].last_h_flood_ms = now;
                    MR_EMIT("xl_handoff_h_flood", EF_I("ctr", h.ctr), EF_I("dst_hash", static_cast<int64_t>(h.dst_key_hash32)), EF_I("leaf", leaf));
                }
                continue;                                            // keep deferred -> re-resolve on a later visit / the H-answer
            }
        }
        // resolved -> build the relay leg. KEEP the slot until the enqueue SUCCEEDS: a transient queue-full RETRIES
        // next visit (like the deferred path), NEVER drops a resolved transit DM (review HIGH #1).
        if (_active->_tx_queue_n >= kTxQueueCap) {
            MR_EMIT("xl_handoff_queue_full_retry", EF_I("origin", h.origin), EF_I("dst", h.dst_node_id), EF_I("ctr", h.ctr));
            continue;                                          // h.valid stays true -> retried on the next window
        }
        // Loop suppression (moved here from the bridge, 4f): seed THIS leaf so we live_dup our own re-inject.
        seed_seen_origin_on_leaf(leaf, h.origin, h.dst_node_id, h.ctr);
        TxItem it{};
        it.origin = h.origin; it.dst = h.dst_node_id; it.ctr = h.ctr; it.ctr_lo = h.ctr_lo;
        it.flags = h.flags; it.type = h.type; it.is_forward = true; it.is_gw_relay = true; it.previous_hop = 0;
        for (int i = 0; i < 8; ++i) it.nonce_seed[i] = h.nonce_seed[i];   // S1: CRYPTED transit DM keeps its nonce seed on the re-inject leg
        RtEntry* rte = rt_find(h.dst_node_id);                // FRESH budget from THIS (target) leaf's route to the recipient
        const uint8_t rt_hops = (rte && rte->n > 0) ? rte->candidates[0].hops : 1;
        const int rem = static_cast<int>(rt_hops) + protocol::hop_budget_slack;
        it.fwd_remaining = static_cast<uint8_t>(rem > protocol::hop_budget_max_initial ? protocol::hop_budget_max_initial : rem);
        it.fwd_committed = 0;
        it.inner_len = (h.inner_len > protocol::max_payload_bytes_hard_cap) ? protocol::max_payload_bytes_hard_cap : h.inner_len;
        for (uint8_t k = 0; k < it.inner_len; ++k) it.inner[k] = h.inner[k];
        it.enqueue_time_ms = _hal.now();
        _active->_tx_queue[_active->_tx_queue_n++] = it;
        h.valid = false;                                       // consume the slot ONLY after a successful enqueue (review HIGH #1)
        MR_EMIT("xl_handoff_drained", EF_I("origin", h.origin), EF_I("dst", h.dst_node_id), EF_I("ctr", h.ctr), EF_I("leaf", leaf));
    }
    // activate_layer calls become_free() right after this -> the drained relay leg gets serviced in this window.
}

void Node::handle_ack(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pk = parse_ack(std::span<const uint8_t>(bytes, len));
    if (!pk) return;
    const ack_out& k = *pk;
    if (!for_me_dst(k.to) || ((k.mobile_to == 1) != _cfg.is_mobile)) return;   // §mobile 3b/6.4: an ACK to EITHER of our plane ids (node_id OR team_local_id), mobile_to matching our kind; a colliding static id ignores it (byte-identical when _team_local_id==0)
    if (!_active->_pending_tx || !_active->_pending_tx->awaiting_ack || _active->_pending_tx->ctr_lo != k.ctr_lo) return;
    // src-less by design (see handle_cts): to+ctr_lo already identifies the ACK as our next-hop's. The
    // src_hint cross-check is SIM-ONLY, so gate it on availability rather than REJECTING when absent — the
    // old `src_hint < 0 ||` dropped EVERY ack on metal (device src_hint=-1), so the DM never completed.
    if (meta.src_hint >= 0 && static_cast<uint8_t>(meta.src_hint) != _active->_pending_tx->next) return;  // (cf. NACK gate, Lua dv:10300)
    _hal.cancel(kAckTimeoutTimerId);
    _hal.cancel(kRetryBackoffTimerId);                   // drop a stale retry armed by a just-fired ack_timeout
    // Learn the ACK sender (= our next-hop) as a 1-hop neighbour (Lua learn_rx_source / ack_frame).
    // §mobile 3b A1: a last-mile flight's next-hop is a mobile LOCAL id -> keep it OUT of the global rt (same principle as
    // the RTS-learn skip at :47; else rt_find(that id) resolves to the mobile). §6.4: a team DM's next is a team LOCAL id
    // too (addr_len=0 but is_team_peer) -> also skip. addr_len==0 + no team peers on every normal flight -> unchanged.
    // §team-parity T0: plane made explicit (was the default). Static reduction: `false` IS the old default.
    // ✔ §team-parity T2 (§3/T2 row 5) — the LEARN half is DONE by the else-arm below; this guard is UNCHANGED (I2).
    if (!(next_is_local_id(_active->_pending_tx->addr_len, _active->_pending_tx->next))
        && learn_direct_neighbor(_active->_pending_tx->next, protocol::db_to_q4(meta.snr_db), false, /*team_plane=*/false)) schedule_triggered_beacon();
#if MR_FEAT_TEAM
    // ✔ §team-parity T2 (§3/T2 row 5): an ACK from OUR next-hop on a TEAM flight is the strongest 1-hop proof there is —
    // it carried our DATA. Same else-arm algebra as the CTS site: !next_is_local_id false is the only path in, so the arm
    // fires exactly on the traffic the guard excludes; is_team_peer is our own state (no id admitted); team_id==0 ⇒ inert.
    // ⚠ §team-parity T5 — ROW 5's "team confirm" IS REFUSED, AND THE SPEC/T2 NOTE THAT ASKED FOR IT WAS WRONG. T5 built the
    // team bidi plane, so "there is nothing to write it to" no longer applies; the reason is now stronger. MEASURED AT
    // SOURCE: note_link_confirmed has exactly TWO callers in the whole engine — the CTS site (:506/:519) and the beacon
    // heard-set scan (node_beacon.cpp) — so THE STATIC PLANE PERFORMS NO BIDI CONFIRM ON AN ACK EITHER. Adding one here
    // for the team plane would not be parity, it would be a team-ONLY mechanism the static plane lacks (the exact inverse
    // of this arc's mandate), and it would be pure decoration: an ACK is structurally unreachable without a CTS for the
    // same flight (`awaiting_ack` is set only inside do_data_tx / its stash-retry and re-issue twins, all of which run
    // after handle_cts's kCtsToDataGapTimerId), so the team confirm has ALREADY fired microseconds earlier on this very
    // flight and would only restamp an identical second. ⇒ REFUSED as a forced fit, per the gate method's §C. The team
    // LIVENESS confirm does happen here, inside learn_direct_neighbor's team branch. If a future slice ever gives the
    // static plane an ACK-time confirm, add BOTH arms together.
    else if (is_team_peer(_active->_pending_tx->next)
             && learn_direct_neighbor(_active->_pending_tx->next, protocol::db_to_q4(meta.snr_db), false, /*team_plane=*/true)) schedule_triggered_beacon();
#endif
    // R4.2: consume the ACK's piggybacked budget_hint -> learn the next-hop's tier in the FORWARD
    // direction (the NACK only covers the reverse). local_only=true: rerank routes but DON'T dirty /
    // schedule a beacon (so NO triggered-beacon draw on the forward path). Lua dv:10341-10344.
    [[maybe_unused]] int ack_budget_reranked = 0;
    if (k.budget_hint > static_cast<uint8_t>(BudgetTier::healthy)
        && !(next_is_local_id(_active->_pending_tx->addr_len, _active->_pending_tx->next))) {   // §mobile: never re-rank a static route from a mobile/team LOCAL next (mirror the ACK-learn guard)
        const uint8_t tier = (k.budget_hint > static_cast<uint8_t>(BudgetTier::critical))
                             ? static_cast<uint8_t>(BudgetTier::critical) : k.budget_hint;
        // the ACK is from our next-hop (matched above) — use that, not src_hint (sim-only / -1 on device).
        ack_budget_reranked = mark_neighbor_budget_tier(_active->_pending_tx->next,
                                                        tier, "ack_budget", /*local_only=*/true);
    }
    // Inc 3: a warn'd ACK means our next-hop considers us near its airtime cap. Honest back-off — park new
    // DM originations until the warn window expires (the hard receiver-side drop is the backstop). The
    // window self-clears: as our airtime ages out of the neighbour's window it stops setting the bit.
    if (k.warn) {
        _ack_warn_until = _hal.now() + protocol::originator_ack_warn_backoff_ms;
        MR_EMIT("originator_warned_by_ack", EF_I("from", _active->_pending_tx->next), EF_I("ctr_lo", k.ctr_lo),
                EF_I("backoff_until_ms", static_cast<int64_t>(_ack_warn_until)));
    }
    MR_EMIT("ack_rx", EF_I("from", _active->_pending_tx->next), EF_I("origin", _active->_pending_tx->origin),
            EF_I("dst", _active->_pending_tx->dst), EF_I("ctr", _active->_pending_tx->ctr), EF_I("budget_hint", k.budget_hint),
            EF_I("budget_reranked", ack_budget_reranked), EF_I("airtime_warn", k.warn ? 1 : 0));  // ACK is from our next-hop (src_hint=-1 on metal)
    { Push pu{}; pu.kind = PushKind::send_acked; pu.dst = _active->_pending_tx->dst; pu.ctr = _active->_pending_tx->ctr; enqueue_push(pu); }
    _active->_pending_tx.reset();
    become_free();
}

// The sender's NACK handler (dv:10365). A NACK is faster feedback than the timeout:
// LOOP_DUP -> cascade to an alt (or direct giveup); BUSY_RX -> mark the peer blind +
// wait-same-hop (short busy) or requeue (long busy). BUDGET/HOP_BUDGET deferred.
void Node::handle_nack(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pn = parse_nack(std::span<const uint8_t>(bytes, len));
    if (!pn) return;
    const nack_out& n = *pn;
    // §mobile: mirror the ACK gate — a NACK to a mobile/team LOCAL id carries mobile_to=1 (accepted by the mobile, IGNORED
    // by a colliding STATIC id). Without this a static node whose global id == a mobile's local id could mis-consume a NACK
    // meant for the mobile (a spurious back-off/reroute). mobile_to==0 for a static originator -> byte-identical.
    if (!for_me_dst(n.to) || ((n.mobile_to == 1) != _cfg.is_mobile)) return;   // §6.4: not for EITHER of our plane ids (node_id / team_local_id)
    if (!_active->_pending_tx) return;                                       // no flight to react on
    if (_active->_pending_tx->ctr_lo != n.ctr_lo) return;                    // stale (different flight). L9 NOTE: WIRE-bounded — a NACK carries only the 4-bit ctr_lo, not flight_gen, so a NACK for a since-replaced flight with an ALIASED ctr_lo (1/16) can still match here. Fully fixing needs more wire ctr bits (a frame change, out of scope); the LOCAL re-arm paths (retry-stash, nack-wait) are now flight_gen-exact.
    if (meta.src_hint >= 0 && static_cast<uint8_t>(meta.src_hint) != _active->_pending_tx->next) {
        MR_EMIT("nack_drop_unexpected_src", EF_I("from", static_cast<uint8_t>(meta.src_hint)));
        return;
    }
    // Learn the NACK sender (= our next-hop) as a 1-hop neighbour (Lua learn_rx_source / nack_frame).
    // §mobile: same mobile/team LOCAL-id guard as the ACK/CTS learns — never install a local id in the static _rt.
    // §team-parity T0: plane made explicit (was the default). Static reduction: `false` IS the old default.
    // ✔ §team-parity T2 (§3/T2 row 6) — DONE by the else-arm below; this guard is UNCHANGED (I2).
    if (!(next_is_local_id(_active->_pending_tx->addr_len, _active->_pending_tx->next))
        && learn_direct_neighbor(_active->_pending_tx->next, protocol::db_to_q4(meta.snr_db), false, /*team_plane=*/false)) schedule_triggered_beacon();
#if MR_FEAT_TEAM
    // ✔ §team-parity T2 (§3/T2 row 6): a NACK from OUR next-hop on a TEAM flight still proves it heard us and is 1 hop
    // away — the flight failed, the LINK did not. Same else-arm algebra and the same inertness at team_id==0 as rows 3/5.
    // (No bidi/confirm half in this row; the static twin has none either — a NACK confirms reachability, not quality.)
    else if (is_team_peer(_active->_pending_tx->next)
             && learn_direct_neighbor(_active->_pending_tx->next, protocol::db_to_q4(meta.snr_db), false, /*team_plane=*/true)) schedule_triggered_beacon();
#endif
    _hal.cancel(kRtsTimeoutTimerId);                                // faster than the timeout (dv:10390)
    _hal.cancel(kAckTimeoutTimerId);
    _active->_pending_tx->awaiting_cts = false; _active->_pending_tx->awaiting_ack = false;
    PendingTx& pt = *_active->_pending_tx;

    if (n.reason == protocol::nack_reason_loop_dup) {
        [[maybe_unused]] const uint8_t from_next = pt.next;
        mark_tried(pt, pt.next);
        const uint8_t alt = pick_next_cascade_hop(pt);
        if (alt != 0) {                                            // cascade to an alt (NO jitter)
            MR_TELEMETRY(
                EventField f[] = { { .key = "origin",   .type = EventField::T::i64, .i = pt.origin },
                                   { .key = "dst",      .type = EventField::T::i64, .i = pt.dst },
                                   { .key = "ctr",      .type = EventField::T::i64, .i = pt.ctr },
                                   { .key = "from_next", .type = EventField::T::i64, .i = from_next },
                                   { .key = "next",     .type = EventField::T::i64, .i = alt } };
                _hal.emit("path_cascade", f, 5);
                _hal.emit("tx_loop_alt", f, 5); );
            pt.next = alt;
            pt.retries_left = effective_rts_max_retries(pt.requeue_count);
            pt.retry_attempt = 0;                                 // cascade -> a NEW contention context: reset the backoff growth (spec 2026-06-26)
            tx_rts_retry();
        } else {                                                  // LOOP_DUP miss -> DIRECT giveup (NOT requeue, dv:10588)
            MR_TELEMETRY(
                EventField f[] = { { .key = "dst", .type = EventField::T::i64, .i = pt.dst },
                                   { .key = "ctr", .type = EventField::T::i64, .i = pt.ctr } };
                _hal.emit("path_cascade_exhausted", f, 2);
                _hal.emit("rts_giveup", f, 2); );
            giveup_flight(giveup_fail_reason("rts_giveup"), pt.dst, pt.ctr);   // §3-A.5: no_cts (was reason=none)
        }
        return;
    }

    if (n.reason == protocol::nack_reason_busy_rx) {
        const uint64_t now = _hal.now();
        const uint64_t busy_for = static_cast<uint64_t>(n.payload) * protocol::nack_busy_quantum_ms;
        MR_EMIT("nack_rx", EF_I("from", pt.next), EF_I("reason", protocol::nack_reason_busy_rx),
                EF_I("busy_ms", static_cast<int64_t>(busy_for)));
        // §mobile (plane-separation re-audit): only blind a GLOBAL next-hop. A mobile/team LOCAL-id next (addr_len=1 /
        // is_team_peer) must not write the static _blind_until plane (a §18-colliding static route would be blinded).
        // Mirrors the OTHER blind guard (the HOP_BUDGET/BUDGET NACK path). Inert on s18 -> byte-identical.
        if (busy_for > 0 && !(next_is_local_id(pt.addr_len, pt.next))) {   // mark the peer blind, max-merge (dv:10627)
            const uint64_t until = now + busy_for;
            auto bit = _active->_blind_until.find(pt.next);
            _active->_blind_until[pt.next] = (bit != _active->_blind_until.end() && bit->second > until) ? bit->second : until;
            MR_EMIT("blind_observed", EF_I("next", pt.next));
        }
        if (busy_for <= protocol::nack_wait_threshold_ms) {        // short busy -> wait SAME hop
            const int jit = _hal.rand_range(0, static_cast<int>(retry_jitter_ms()) + 1);   // N1 (the only new draw)
            const uint32_t wait = static_cast<uint32_t>(busy_for) + 1 + static_cast<uint32_t>(jit);
            _nack_wait_flight_gen = pt.flight_gen; _nack_wait_pending = true;   // L9: key the BUSY_RX re-RTS wait on the exact flight (was pt.ctr_lo)
            (void)_hal.after(wait, kNackWaitTimerId);
        } else {                                                  // long busy -> requeue SAME hop (verbatim meta)
            TxItem it = txitem_from_pending(pt);   // S1: full identity+crypto core (incl. nonce_seed — the uncited long-busy drop)
            it.requeue_count = pt.requeue_count; it.enqueue_time_ms = pt.enqueue_time_ms;   // VERBATIM (no ++/backoff)
            it.next_attempt_ms = 0;
            MR_EMIT("tx_requeued", EF_I("dst", pt.dst), EF_I("ctr", pt.ctr));
            if (_active->_tx_queue_n < kTxQueueCap) {
                _active->_tx_queue[_active->_tx_queue_n++] = it;
            } else {                                              // queue full -> can't requeue; give up loudly
                MR_TELEMETRY(
                    EventField gf[] = { { .key = "dst", .type = EventField::T::i64, .i = pt.dst },
                                        { .key = "ctr", .type = EventField::T::i64, .i = pt.ctr } };
                    _hal.emit("path_cascade_exhausted", gf, 2);
                    _hal.emit("rts_giveup", gf, 2); );
                push_send_failed(giveup_fail_reason("rts_giveup"), pt.dst, pt.ctr);   // §3-A.5: no_cts (was reason=none). NOT giveup_flight: the reset+become_free below are shared with the requeue arm above.
            }
            _active->_pending_tx.reset();
            become_free();
        }
        return;
    }

    if (n.reason == protocol::nack_reason_hop_budget) {
        // TERMINAL (dv:10487): the route was longer than the budget assumed. Bump
        // rt[dst].hops UPWARD so a FUTURE send (new ctr) budgets correctly, then DROP
        // (no retry/cascade — a same-ctr retry would recompute the same too-small
        // budget). committed (the NACK payload high nibble) is a lower bound on the
        // true distance. This bump feeds do_data_tx's initial-budget computation.
        const uint8_t committed = static_cast<uint8_t>((n.payload >> 4) & 0x0f);
        RtEntry* e = rt_find(pt.dst);
        if (e != nullptr && e->n > 0) {
            const int want = (committed + 1 > e->candidates[0].hops) ? (committed + 1)
                                                                     : e->candidates[0].hops;
            const uint8_t new_hops = static_cast<uint8_t>(want > 15 ? 15 : want);   // 4-bit DV field clamp
            if (new_hops != e->candidates[0].hops) {
                e->candidates[0].hops = new_hops;
                // §3-A.5: route through the shared emit_rt_update (was a hand-rolled 4-field emit missing `score`); keep the
                // slot string. Carries the route's current score so the rt_update schema matches the beacon-merge sites.
                emit_rt_update(_hal, pt.dst, e->candidates[0].next_hop, e->candidates[0].score, new_hops, "hop_budget_nack");
            }
        }
        MR_EMIT("nack_rx", EF_I("from", pt.next), EF_I("reason", protocol::nack_reason_hop_budget), EF_I("committed", committed));
        MR_TELEMETRY(
            EventField gf[] = { { .key = "dst", .type = EventField::T::i64, .i = pt.dst },
                                { .key = "ctr", .type = EventField::T::i64, .i = pt.ctr } };
            _hal.emit("path_cascade_exhausted", gf, 2);
            _hal.emit("rts_giveup", gf, 2); );
        giveup_flight(giveup_fail_reason("rts_giveup"), pt.dst, pt.ctr);   // §3-A.5: no_cts (was reason=none)
        return;
    }

    if (n.reason == protocol::nack_reason_budget) {
        // R4.1 (Lua dv:10406-10453): the next hop refused on its own duty budget. Blind it for a
        // tier-scaled window + requeue the flight (the re-issue skips the now-blind hop via
        // pick_next_cascade_hop -> alt, or originator-defer / forwarder-drop). The blind window is
        // short-term "don't try right now"; the routing-grade persistent demotion is R4.2.
        const uint8_t tier = static_cast<uint8_t>((n.payload >> 4) & 0x0f);                // inline decode
        uint32_t blind_ms = protocol::budget_blind_critical_ms;                            // tier==CRITICAL default
        if      (tier >= static_cast<uint8_t>(BudgetTier::exhausted)) blind_ms = protocol::budget_blind_exhausted_ms;
        else if (tier <= static_cast<uint8_t>(BudgetTier::strained))  blind_ms = protocol::budget_blind_strained_ms;
        const uint64_t until = _hal.now() + blind_ms;                                      // max-merge (dv:10416-10422)
        auto bit = _active->_blind_until.find(pt.next);
        if (!(next_is_local_id(pt.addr_len, pt.next))                        // §mobile: never blind a static route on a mobile/team LOCAL next (mirror the NACK-learn guard)
            && (bit == _active->_blind_until.end() || until > bit->second)) {
            _active->_blind_until[pt.next] = until;
            MR_EMIT("blind_observed", EF_I("next", pt.next));
        }
        // R4.2: record the persistent neighbor tier (routing-grade demotion beyond the blind window)
        // + rerank affected routes. local_only=false -> dirty + a triggered beacon if a primary moved.
        // Reads pt.next BEFORE try_cascade_requeue resets _active->_pending_tx.
        [[maybe_unused]] const int reranked = (next_is_local_id(pt.addr_len, pt.next))   // §mobile: never re-rank a static route from a mobile/team LOCAL next
            ? 0 : mark_neighbor_budget_tier(pt.next, tier, "nack_budget", /*local_only=*/false);
        MR_EMIT("nack_rx", EF_I("from", pt.next), EF_I("reason", protocol::nack_reason_budget), EF_I("tier", tier),
                EF_I("reranked", reranked));
        // requeue-or-giveup: the helper does both legs (caps -> exhausted+giveup+drop, else
        // requeue@backoff) + _active->_pending_tx.reset() + become_free()/timer (dv:10449-10467). The caps
        // giveup event is "rts_giveup" (Lua dv:10462; "budget_low" is the trigger, not the name).
        try_cascade_requeue(pt, "rts_giveup");
        return;
    }

    // BUDGET tier > CRITICAL etc. all handled above. Any other (future) reason: defensive restore
    // of awaiting_cts + re-arm so an unexpected NACK doesn't strand the flight (timeouts cancelled above).
    pt.awaiting_cts = true;
    start_rts_timeout();
}

}  // namespace meshroute
