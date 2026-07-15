// MeshRoute — lib/core/node_carriers.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The Node's shared VALUE-CARRIER types — LayerConfig/NodeConfig/RtCandidate/RtEntry/TxItem/PendingTx/DeferredSend/
// PendingRx/PostAck/LastAcked/GatewaySchedule/BridgedLayer/XlHandoff + the enums (LinkBidi/Plane/GwValErr/GwParseErr).
// Extracted VERBATIM from node.h (cleanup 2026-07-15, node-legibility Slice 2): shared across all 13 node_*.cpp TUs and
// tied to no single member, so they get a coherent home here instead of ~370 lines of scroll-past clutter above class
// Node. The member-SPECIFIC nested structs stay nested in node.h (def-next-to-member co-location = their legibility).
// namespace-scope + protocol:: constants only (no Node/frame_codec deps) -> layout-identical, 0 external ref changes.
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute
#endif
#include <cstdint>
#include "protocol_constants.h"

namespace MESHROUTE_NS {

// Per-layer (per-leaf) config — the dual-layer gateway model (2026-06-12-gateway-dual-layer-design.md §3.1).
// A normal node has n_layers=1 (uses layers[0]); a GATEWAY has 2 EQUAL layers (no home/guest, §0.1). One
// identity (key_hash32) spans both; node_id is per-leaf (independent DAD, §0.2). `layer_id` is the FULL 8-bit
// id (1..255); `leaf_id = layer_id & 0x0F` is the derived coarse wire filter (§0.8). window_ms/offset 0 =
// DERIVE the SF-weighted anti-phase split at the scheduler (§4, Slice 3). REQUIRED on a gateway: layer_id /
// routing_sf / allowed_sf_bitmap — on_init fails LOUD if unset (§3.2, no silent inherit).
struct LayerConfig {
    uint8_t  layer_id          = 0;       // FULL 8-bit id (1..255); 0 = unset. leaf_id = layer_id & 0x0F.
    uint8_t  node_id           = 0;       // per-leaf 8-bit address (independent DAD, §0.2); 0 = unprovisioned.
                                          // Slice 3: STATIC (provisioned via cfg/NV). Single-layer node_id DAD is BUILT
                                          // (node_join.cpp); only GATEWAY per-leaf DAD stays deferred (two independent claims, undesigned).
    uint8_t  routing_sf        = 0;       // 0 = unset (REQUIRED per layer)
    uint16_t allowed_sf_bitmap = 0;       // allowed DATA-SF set; 0 = unset/no-data-SF (REQUIRED per layer)
    double   freq_mhz          = 0.0;     // per-layer RF carrier; 0 = inherit the node's boot/global freq. A layer
                                          // is a (freq, SF, leaf) channel — the gateway retunes freq on a window switch.
    uint32_t bw_hz             = 0;       // per-layer bandwidth (Hz); 0 = inherit the node's global radio_bw_hz.
    uint8_t  cr                = 0;       // per-layer coding-rate (5..8); 0 = inherit radio_cr. A layer is a full
                                          // (freq, SF, BW, CR) channel — BW/CR retune with SF/freq on a window switch.
    uint32_t beacon_period_ms  = 900000;
    uint32_t window_period_ms  = 15000;   // the full layer0->layer1 cycle (§3.2 default; cfg-overridable)
    uint32_t window_ms         = 0;       // this layer's presence in the cycle; 0 = DERIVE SF-weighted (§4)
    uint32_t window_offset_ms  = 0;       // phase; 0 = DERIVE anti-phase from the other layer (§4)
};

// §3.2 dual-layer validation result. The SHARED predicate validate_gateway_layers() returns this; on_init maps
// non-ok -> refuse (bool false), the `gateway` console command maps it to a specific operator error message.
enum class GwValErr : uint8_t {
    ok = 0, bad_leaf, bad_ctrl_sf, no_data_sf, leaf_nibble_clash,
    period_mismatch, period_zero, window_degenerate, window_zero, window_exceeds_period, window_overlap, window_too_long,
    bad_bw, bad_cr   // v17 per-layer PHY: bw_hz not a legal SX1262 bandwidth / cr not in 5..8 (0 = inherit is always ok)
};
// The ONE dual-layer gate, shared by on_init AND parse_gateway_cmd (so the console command can never accept a
// config on_init would refuse — anti-drift). Validates both layers' required fields + the leaf-nibble rule + the
// shared window period, DERIVES the SF-weighted anti-phase window split for any window_ms/offset left 0, and
// validates the concrete schedule. MUTATES L0/L1 window fields in place. Pure (no Serial/NV).
GwValErr validate_gateway_layers(LayerConfig& l0, LayerConfig& l1, uint32_t radio_bw_hz, uint8_t radio_cr);

// `gateway` console command parse result. Parse-stage errors (format / per-field ranges); the cross-layer +
// window checks are validate_gateway_layers' job (the caller runs it after a clean parse).
enum class GwParseErr : uint8_t {
    ok = 0, missing_l0, missing_l1, bad_l0, bad_l1, bad_leaf, bad_node, bad_ctrl_sf, bad_data_sf,
    bad_period, bad_window, bad_beacon, bad_freq, unknown_opt
};
struct GatewayProvision {
    LayerConfig l0{};            // filled: layer_id, node_id, routing_sf, allowed_sf_bitmap (+ window_* after validate-derive)
    LayerConfig l1{};
    bool gateway_only = false;
    uint32_t beacon_ms = 0;      // optional `beacon=`; 0 = UNSPECIFIED -> caller preserves the existing per-layer cadence
};
// Parse "l0=<leaf>:<node>:<ctrl_sf>:<data_sfs> l1=… [period=ms] [win0=ms:off] [win1=ms:off] [beacon=ms] [freq0=MHz] [freq1=MHz] [gateway_only=0|1]"
// into `out` (l0/l1 LayerConfigs + opts). Per-field ranges only: leaf 1..255, node 1..254 (the `1..16` gateway
// reservation is NOT enforced here — Join-time), ctrl_sf 5..12, non-empty data SFs. The cross-layer/nibble/window
// gate is validate_gateway_layers (run by the caller). Pure: no Serial/NV. `out` is fully overwritten on ok.
GwParseErr parse_gateway_cmd(const char* args, GatewayProvision& out);

// POD; no heap, no JSON. Only the T/F-class knobs the Lua on_init reads.
// PROTOCOL constants stay in protocol_constants.h (hardcoded on device).
// Leaf-membership / identity / join is DESIGN-RESOLVED — see docs/specs/2026-06-05-identity-leaf-membership-join-design.md:
// name lives in the identity record; NO BCN crypt key (fingerprint gate, honest-node); DM-only crypto; JOIN carries a
// 1-byte wire-version (wire-compat, not node version). The leaf-config half is implemented in R6 (PORT_PLAN §9).
struct NodeConfig {
    bool     is_gateway          = false;
    bool     gateway_only        = false;       // §7 flood switch: true = PURE bridge (out of the channel plane);
                                                // false (default) = gateway ALSO serves its owner (consumer half on, provider half off)
    bool     is_mobile           = false;
    uint32_t team_id             = 0;           // §mobile 6.1: an is_mobile+team_id overlay; 0 = no team (lone mobile / any static node) = today's behaviour. team_id = hash(creator_key‖nonce). Read by 6.2 (routing) / 6.3 (channel).
    bool     mobile_autoregister = true;        // §mobile console: gate ALL autonomous mobile behaviour (boot-arm + home-lost re-scan + re-CLAIM + auto layer-pull). ON = today. OFF = the app drives every step via `mobile register`/`query`.
    bool     host_mobiles        = true;        // §mobile 2a: this static node accepts/hosts mobiles (activates the J DISCOVER->OFFER->CLAIM host side). Opt-out = false; a mobile itself never hosts.
    bool     join_required       = false;
    bool     req_sync_on_boot    = true;
    bool     seen_bitmap_enabled = false;   // OFF by default 2026-06-19: no measurable delivery benefit in ANY scenario
                                            // (freshness is next-hop-local + reception-driven; the reactive liveness plane
                                            // owns disappearing-node detection — see is_next_hop_fresh/record_peer_rts_timeout),
                                            // and it costs cross-layer delivery (s15/s16). Code retained, config-enableable.
    uint8_t  routing_sf          = 7;
    uint16_t allowed_sf_bitmap   = 0;            // allowed DATA-SF set (bit=sf), from config allowed_data_sfs (sf_list);
                                                 // 0 = no data SF -> node refuses to originate data + ignores data RTS
    uint32_t beacon_period_ms    = 900000;
    uint32_t beacon_max_idle_ms  = 900000;
    uint8_t  req_sync_min_routes = 8;            // originator: stop REQ_SYNC once rt reaches this (Lua dv:8039)
    bool     sync_response_enabled    = true;    // responder: answer an overheard REQ_SYNC with a jittered full-table beacon (Lua dv:8936)
    uint8_t  sync_response_min_routes = 0;       // responder gate (Lua nil -> 0: respond even when route-starved, dv:8067)
    uint8_t  channel_dirty_max_advertisements = protocol::channel_dirty_max_advertisements;  // K: retire a dirty channel id after this many BCN digests (Lua node.channel_dirty_max_advertisements or 3); per-node so a gate can shrink it
    uint32_t channel_pull_jitter_ms       = protocol::channel_pull_jitter_ms;        // digest-pull backoff range rand(0,J) (Lua node.channel_pull_jitter_ms); a gate shrinks it to pin pull order
    // (antispam-v2 Slice 3: the flat channel_origin_max_per_window cap was removed — channel_origin_admit now
    // enforces the duty-anchored channel_cap_origin(); the ledger is sized by protocol::cap_channel_origin_events.)
    uint32_t channel_origin_window_ms     = protocol::channel_origin_window_ms;      // sliding window for the per-origin cap (Lua node.channel_origin_window_ms)
    uint8_t  cap_route_request_last        = protocol::cap_route_request_last;        // per-dst RREQ rate-limit table cap (Lua node.cap_route_request_last); full -> refuse new dsts (table_cap_hit). Shrinkable; array stays sized at the protocol max.
    uint16_t cap_id_bind                   = protocol::cap_id_bind;                    // hash-locate binding table cap (Lua node.cap_id_bind); full -> refuse new node_ids (table_cap_hit). Shrinkable; a gate sets it to 2.
    uint32_t id_bind_ttl_ms                = protocol::id_bind_ttl_ms;                 // hash-locate binding TTL (Lua node.id_bind_ttl_ms, 48h); a gate shrinks it to exercise aging
    uint32_t quiet_threshold_ms  = 30000;        // beacon throttle gate; <=0 = unthrottled (R1 fast path)
    uint8_t  leaf_id             = 0;            // layer id (single-layer R1 = 0)
    // R6.1 leaf-config membership: lineage_id 0 = UNMANAGED leaf (peer-by-config_hash, backward-compat); a
    // `leaf create` mints a non-zero lineage. config_epoch is LWW (ties -> higher key_hash32). leaf_name is in the
    // config_hash (a change re-fingerprints the leaf). config_hash itself is DERIVED (leaf_config_hash over the cfg).
    uint16_t lineage_id          = 0;       // u16 (2026-06-20b right-size)
    uint16_t config_epoch        = 0;
    uint8_t  leaf_name_len       = 0;
    char     leaf_name[protocol::leaf_name_max] = {};
    uint16_t peer_count          = 0;            // host-set (N-1); 0 = no rt_full emit (sim telemetry)
    // R2 route-aging TTLs (config-overridable so a gate can shrink them; Lua
    // reads `config.X or <constant>`). hops<=1 uses neighbor, else remote.
    uint32_t rt_aging_ttl_neighbor_ms = protocol::rt_aging_ttl_neighbor_ms;  // 45 min
    uint32_t rt_aging_ttl_remote_ms   = protocol::rt_aging_ttl_remote_ms;    //  3 h
    uint32_t rt_aging_check_period_ms = protocol::rt_aging_check_period_ms;  // 60 s
    // R3 data plane: radio params for floor-exact airtime (timeout/retry sizing).
    uint32_t radio_bw_hz = 250000;
    uint8_t  radio_cr    = 5;
    uint8_t  dv_hop_cap  = protocol::dv_hop_cap;  // DV route hop cap + F RREQ TTL. Network-wide: set via the J join
                                                  // frame (Slice 3); static config is the bootstrap/fallback. Default 16.
    // R4.0 duty-cycle budget. Default OFF (0.0) so every prior gate stays HEALTHY/inert; a
    // budget scenario sets duty_cycle explicitly. budget_ms = floor(duty_cycle*window) at on_init.
    // (Lua default is 0.01; we default OFF — see spec §2. Lua dv:8495-8497.)
    double   duty_cycle           = 0.0;        // fraction of the window we may transmit; <=0 = disabled
                                                // (double, NOT float: floor(0.01*window) must match the Lua's
                                                //  double exactly — float 0.01f*3.6e6 floors to 35999, not 36000)
    uint32_t duty_cycle_window_ms = 3600000;    // rolling airtime window (1 h)
    // Anti-spam v2 (2026-06-30): the fraction of the route-table size treated as ACTIVE channel originators, for the
    // per-origin channel cap's 1/N sharing (N_active = max(1, floor(frac * rt_count()))). A deployment knob, NOT a wire
    // const. Seed 0.125. NOTE: N_active floors at 1, so this is INERT for rt_count() < 8.
    float    channel_active_fraction = 0.125f;
    // Anti-spam v2 forced-delay burst floors (promoted to per-leaf provisioned config 2026-07-03). Factory defaults =
    // the protocol_constants of the same name; a mother provisions them in the C config frame (leaf_config.{h,cpp}) and
    // they ARE in the config_hash (a change re-fingerprints -> joiners auto-resync). Enforced live (the MAC re-reads
    // these each use): channel floor at channel_origin_admit + do_send_channel; DM floor in become_free + issue_send.
    uint32_t channel_min_interval_ms = protocol::channel_min_interval_ms;   // 10 s — per-origin channel burst floor
    uint32_t dm_min_interval_ms      = protocol::dm_min_interval_ms;        //  3 s — own-DM burst floor
    // Gateway noise control: a gateway is REACTIVE-ONLY in steady state (beacons on dirty state / REQ_SYNC only).
    // Its sole unsolicited steady-state announcement is a slow safety-net heartbeat, allowed ONLY when BOTH hold:
    //   (a) current rolling airtime < gw_announce_duty_pct % of the duty budget (headroom), and
    //   (b) >= gw_announce_min_interval_ms since the last beacon. Discovery still announces on the fast cadence
    //   (a NEW gateway / two-layer link-up must be discoverable). Both configurable; 5% / 3 h defaults.
    uint8_t  gw_announce_duty_pct       = 5;          // % OF the duty budget (e.g. 5% of a 10% duty = 0.5% airtime)
    uint32_t gw_announce_min_interval_ms = 10800000;  // 3 h floor between unsolicited steady-state announcements
    // §3e herd-spread slack: the herd-jitter spread (and its window-tail headroom) is sized as exchange_airtime_ms() ×
    // this factor. The bare exchange airtime is collision-UNSAFE for uniform-random placement (N senders back-to-back
    // still birthday-collide); the slack supplies the headroom (the Lua's fixed 600ms was airtime ≈358ms × ~1.7 of
    // implicit slack). Default 2; cfg-tunable. 1 = bare airtime (no slack), 0 treated as 1.
    uint8_t  gw_herd_slack             = 2;
    uint16_t originator_max_per_window = 6;      // R4.4 anti-spam: apparent_origination drop threshold (T-class)
    uint32_t beacon_silence_jitter_ms  = 10000;  // R4.3 adaptive-throttle deferred-TX spread (dv:921)
    // R4.5 listen-before-talk. lbt_enabled default true (Lua dv:8625). The two delays default to 0 = "derive
    // in on_init" (lbt_backoff_ms = max(1, retry_jitter_ms/2); flood_lbt_max_defer_ms = airtime(beacon_max_bytes)).
    bool     lbt_enabled               = true;
    uint32_t lbt_backoff_ms            = 0;       // 0 => derive
    uint32_t flood_lbt_max_defer_ms    = 0;       // 0 => derive
    bool     nav_enabled               = true;    // NAV virtual carrier sense ON by default (device + sim consistent). C++-only — so it DIVERGES the lua↔meshroute differentials by design (Lua is frozen); set false to restore lua-parity (e.g. in the differential scenarios).
    bool     nav_ignore_rts            = false;   // NAV: ANSWER an addressed RTS even during a reservation (sim-tuned default). true = drop it (802.11 blanket-NAV) — protects the reservation but causes cascades/giveups. ignore-off won on s18 + s17_metro: same delivery, fewer collisions + cascades.
    bool     intra_layer_relay         = false;   // §gateway: relay OTHER nodes' same-leaf DMs? default OFF — a gateway is a
                                                  // cross-layer bridge, not an intra-leaf relay (design 2026-06-12 §6). Live-only
                                                  // like nav_enabled (an opt-in; reverts to OFF on reboot — the default IS the fix).
    // ---- opt-in location propagation (2026-06-14 spec). Default OFF -> the flag/slot never appear -> s18 byte-identical.
    int32_t  lat_e7 = 0, lon_e7 = 0;             // this node's location (deg×1e7; (0,0) = unset -> NEVER transmitted)
    bool     loc_in_dm = false;                  // piggyback location on originated DMs (DATA_FLAG_LOCATION, sealed inner)
    bool     loc_in_m  = false;                  // piggyback location on originated channel M frames (flavor 0x08, public)
    bool     e2e_dm    = false;                  // Phase 1: originate app DMs ENCRYPTED when the recipient's pubkey is known; default OFF -> s18 byte-identical
    // ---- dual-layer gateway (2026-06-12 design). n_layers=1 = normal node (uses layers[0]); 2 = gateway.
    //      Slice 0: layers[0] MIRRORS the legacy routing_sf/allowed_sf_bitmap/leaf_id/beacon_period_ms scalars
    //      (set in on_init). NB: `is_gateway` is DERIVED, NOT configurable — on_init forces `is_gateway = (n_layers==2)`
    //      (node.cpp:204; the pre-3c "single-layer channel-plane gw_env" notion is SUPERSEDED). So `is_gateway` ≡
    //      `n_layers==2`, and per Principle 11 a dual-layer gateway skips the channel gossip plane ENTIRELY (gated on
    //      n_layers==2 at every channel entry — justifies cap_channel_buffer=8). (The bidi census gate `n_layers!=2` ≡ `!is_gateway`.)
    uint8_t     n_layers = 1;
    LayerConfig layers[2];
};

// Per-next-hop bidirectionality state (asymmetric-link plane, 2026-06-29). unknown=0 so a zeroed
// _link_bidi slot defaults to 'not yet probed' (selectable, unpenalized). confirmed = a real CTS or
// a complete-heard-set hit; one_way = positive absent+complete evidence (NEVER mere staleness — see
// decay_link_bidi). Packed as a uint8_t array per LayerRuntime (room to grow).
enum class LinkBidi : uint8_t { unknown = 0, confirmed = 1, one_way = 2 };

// One route candidate (DV). Mirrors the Lua rt[dest].candidates[i] fields
// (dv_dual_sf.lua:9646-9654). score is Q4 dB. effective_score = score − budget_penalty
// (R4.2; == score for a HEALTHY-tier next_hop) − suspect_penalty (0, deferred plane).
struct RtCandidate {
    uint8_t  next_hop         = 0;
    int16_t  score            = 0;   // Q4 dB
    uint8_t  hops             = 0;
    uint64_t last_seen_ms     = 0;
    uint8_t  n2_hop           = 0;   // advertised next-hop (for the R2 3-cycle prune)
    bool     is_gateway       = false;
    uint8_t  learned_leaf = 0;            // the neighbour's leaf nibble (layer & 0x0F) — all byte-0 exposes; NOT the full layer id
    bool     degraded_from_wire = false;   // the WIRE-inherited degraded component ONLY (a fact about what the
                                           // advertiser advertised). The LIVE degraded state is recomputed as
                                           // degraded_from_wire || _link_bidi[next_hop]==one_way (candidate_degraded) — NEVER a sticky OR.
};
struct RtEntry {
    uint8_t     dest = 0;
    RtCandidate candidates[protocol::max_rt_candidates];
    uint8_t     n     = 0;           // candidates in use (1..K)
    bool        dirty = false;       // set when candidates[0] changes (R2/R4 dirty-only beacons)
};

// ---- R3 data-plane state (MAC) ---------------------------------------------
// inner = [dst_key_hash32 (iff DST_HASH)]|origin(1)|body — the DATA unicast inner (parse_unicast_inner;
// no payload-flags byte). flags = the byte-1 DataFlag set; type = the byte-8 DataType (0 = normal DM).
// §mobile 6.4 / Wave 2: the addressing PLANE a send/route uses. AUTO = dispatch by is_team_peer (today's behaviour,
// byte-identical); TEAM = force the team plane (_rt_team + team_local_id link src); GLOBAL = force the global/static
// plane (_rt + node_id), never the team plane even for an id that COLLIDES a teammate's team id.
enum class Plane : uint8_t { AUTO = 0, TEAM = 1, GLOBAL = 2 };

struct TxItem {                      // a queued message awaiting a flight
    uint8_t  origin = 0, dst = 0, ctr_lo = 0;
    uint16_t ctr = 0;
    uint8_t  flags = 0;
    uint8_t  type = 0;               // DataType (0 = normal DM); threaded so a forward keeps its frame type
    uint8_t  inner[protocol::max_payload_bytes_hard_cap] = {};
    uint8_t  inner_len = 0;
    uint8_t  nonce_seed[8] = {};     // CRYPTED only: the 8-B XChaCha nonce-seed -> the DATA MAC trailer at do_data_tx
    bool     is_forward = false;     // true => previous_hop valid (a relayed item)
    uint8_t  previous_hop = 0;
    // Cascade-requeue meta (the Lua queue_meta): requeue_count drives the
    // exponential backoff cap; enqueue_time_ms is the ORIGINAL first-enqueue
    // time, preserved across every requeue so the total-age cap is honest;
    // next_attempt_ms gates the dequeue so the backoff can't be skipped by a
    // concurrent become_free (the queue itself enforces the hold).
    uint8_t  requeue_count = 0;
    uint64_t enqueue_time_ms = 0;
    uint64_t next_attempt_ms = 0;
    // Hop-budget carried forward on a relayed item (a forwarder's already-decremented
    // values; originators recompute from rt). Ignored unless is_forward.
    uint8_t  fwd_remaining = 0;
    uint8_t  fwd_committed = 0;
    // Channel M-broadcast (gossip plane): the data-SF frame is the lean M frame (cmd 0xA), fire-and-forget.
    bool     is_channel_m = false;   // true => a channel M-broadcast (flood OR pull-response); flags is unused (no DM)
    // Channel FLOOD m-broadcast (2026-06-08): the 43-B FLOOD RTS-M tail rides the flight.
    bool     flood = false;          // true => FLOOD RTS-M (vs legacy M_BROADCAST); a true broadcast (next=0xFF, no route)
    uint8_t  hop_left = 0;           // FLOOD TTL safety cap (rides the RTS `dst` slot, §3.1)
    uint8_t  flood_bitmap[32] = {};  // FLOOD coverage bitmap (carried into the RTS-M tail)
    bool     is_gw_relay = false;    // Slice 4c.1: a gateway's cross-layer re-inject -> exempt from originator anti-spam (wired 4c.2)
    uint8_t  addr_len   = 0;         // §mobile 3a: 0=normal, 1=mobile-next (this DM's next-hop is a mobile LOCAL id) -> RTS byte-3
    bool     mobile_src = false;     // §mobile 3a: originator is a mobile (set in 3b outbound; 0 for a host forward) -> RTS byte-5 b1
    Plane    plane      = Plane::AUTO;// Wave 2: the addressing plane (AUTO=dispatch by is_team_peer; TEAM/GLOBAL forced)
};
struct PendingTx {                   // the in-flight sender state (one per node)
    uint8_t  origin = 0, dst = 0, next = 0, ctr_lo = 0;
    uint16_t ctr = 0;
    uint8_t  flags = 0;
    uint8_t  type = 0;               // DataType (0 = normal DM); carried into pack_data at do_data_tx
    uint8_t  addr_len   = 0;         // §mobile 3a: 0=normal, 1=mobile-next (carried from the TxItem -> RTS byte-3)
    bool     mobile_src = false;     // §mobile 3a: originator is a mobile (-> RTS byte-5 b1); 0 for a host forward
    Plane    plane      = Plane::AUTO;// Wave 2: carried from the TxItem -> plane-aware route dispatch (pick_next_cascade_hop)
    uint8_t  inner[protocol::max_payload_bytes_hard_cap] = {};
    uint8_t  inner_len = 0;
    uint8_t  nonce_seed[8] = {};     // CRYPTED only: the 8-B nonce-seed (from the TxItem) -> the DATA trailer at do_data_tx
    uint8_t  chosen_data_sf = 0;     // 0 = unset until the CTS arrives
    bool     m_broadcast    = false; // channel M-payload: fire-and-forget (no CTS/ACK); chosen_data_sf set at issue
    uint8_t  retries_left = 0;
    uint8_t  retry_attempt = 0;      // same-hop retry # (0,1,2,...) -> the capped-exponential backoff shift; reset to 0 on a fresh flight / cascade-to-alt (a new contention context). Internal, NOT on-wire.
    uint64_t timeout_deadline_ms = 0;// absolute fire time of the armed CTS/ACK timeout (start_rts/ack_timeout) -> reserve_yield extends-only (never shortens) when yielding to an overheard reserve. Internal.
    bool     awaiting_cts = false;
    bool     awaiting_ack = false;
    // Cascade-to-alt state: which next-hops this flight has already tried (so the
    // walk never re-picks them), the upstream hop to avoid looping back to, and
    // the requeue meta threaded from the TxItem.
    uint8_t  previous_hop = 0;
    bool     has_previous_hop = false;
    uint8_t  alts_tried[protocol::max_rt_candidates] = {};
    uint8_t  alts_tried_n = 0;
    uint8_t  requeue_count = 0;
    uint64_t enqueue_time_ms = 0;
    // Hop-budget for a forwarded flight (inherited from the received DATA, already
    // decremented in handle_data). For an originator, do_data_tx recomputes from rt.
    uint8_t  fwd_remaining = 0;
    uint8_t  fwd_committed = 0;
    // Monotonic flight identity (bumped on each new pending_tx at issue_send). The C++ equivalent of the Lua's
    // object-identity guard `__pending_tx_ref` (dv:3712) — an exact staleness key for a deferred RTS, replacing the
    // 4-bit ctr_lo proxy that wraps every 16 sends (cleanup #A redo). cascade_to_alt mutates in place (same gen).
    uint32_t flight_gen = 0;
    // Channel FLOOD m-broadcast: the 43-B FLOOD RTS-M tail (copied from the TxItem at issue_send).
    bool     flood = false;
    uint8_t  hop_left = 0;
    uint8_t  flood_bitmap[32] = {};
    bool     is_gw_relay = false;    // Slice 4c.2: a gateway's cross-layer re-inject -> RTS carries RTS_FLAG_RELAY (receiver exempts it from anti-spam)
};
// S1 (2026-07-04): the ONE place a TxItem is re-materialized from an in-flight PendingTx. Every
// requeue site (try_cascade_requeue / gateway_doorstep_hold / the long-busy same-hop requeue) MUST
// route through this so a field added to the identity+crypto set can never be forgotten at one site
// again — the field-drop class (H4/M7: `type`, `nonce_seed`) that made CRYPTED DMs undeliverable and
// typed frames deliver as junk. Copies the FULL shared core; site-specific meta (requeue_count,
// enqueue_time_ms, next_attempt_ms) is applied by the caller AFTER. When you add a shared field to
// BOTH TxItem and PendingTx, add its copy HERE (the single update point).
//   Shared core copied: origin, dst, ctr_lo, ctr, flags, type, inner[+inner_len], nonce_seed[8],
//   is_forward(<-has_previous_hop)/previous_hop, is_gw_relay, fwd_remaining, fwd_committed.
//   NOT copied (site meta, set by the caller): requeue_count, enqueue_time_ms, next_attempt_ms.
static inline TxItem txitem_from_pending(const PendingTx& pt) {
    TxItem it{};
    it.origin = pt.origin; it.dst = pt.dst; it.ctr_lo = pt.ctr_lo; it.ctr = pt.ctr;
    it.flags = pt.flags; it.type = pt.type;
    it.inner_len = pt.inner_len;
    for (uint8_t i = 0; i < pt.inner_len; ++i) it.inner[i] = pt.inner[i];
    for (int i = 0; i < 8; ++i) it.nonce_seed[i] = pt.nonce_seed[i];     // CRYPTED nonce seed — the H4 drop
    it.is_forward = pt.has_previous_hop; it.previous_hop = pt.previous_hop;
    it.is_gw_relay = pt.is_gw_relay;                                     // a requeued cross-layer relay keeps RTS_FLAG_RELAY
    it.fwd_remaining = pt.fwd_remaining; it.fwd_committed = pt.fwd_committed;   // carry the hop budget across the requeue
    it.addr_len = pt.addr_len; it.mobile_src = pt.mobile_src;            // §mobile 3a: a requeued last-mile forward keeps the mobile marks
    return it;
}
struct DeferredSend {                // a send with no route yet — held until one appears (or TTL)
    TxItem   item;
    uint64_t deferred_at_ms = 0;     // for the send_defer_ttl giveup (TTL checked FIRST on drain)
};
struct PendingRx {                   // the receiver state awaiting DATA (one per node)
    uint8_t  from = 0, dst = 0, ctr_lo = 0, chosen_data_sf = 0, payload_len = 0;
    uint64_t set_at_ms = 0;
    uint64_t expiry_ms = 0;          // absolute DATA-wait expiry (for the BUSY_RX NACK busy_for calc)
    bool     claimed_e2e_ack = false;// the RTS carried RTS_FLAG_E2E_ACK (backstop DROP exempted). Verified at DATA-time:
                                     // if the DATA is NOT a DATA_TYPE_E2E_ACK the sender lied -> flag it (e2e_ack_spoof).
    bool     mobile_from = false;    // §mobile: the RTS was mobile_src -> `from` is a home-assigned LOCAL id, NOT a global
                                     // identity -> the DATA-time learn (node_mac_rx.cpp) MUST NOT install it in the static _rt.
};
struct PostAck {                     // deferred deliver/forward after the ACK airtime
    bool     pending = false;
    bool     is_forward = false;     // false => deliver (dst==self); true => forward
    uint8_t  origin = 0, dst = 0, ctr_lo = 0, previous_hop = 0;
    uint16_t ctr = 0;
    uint8_t  flags = 0;
    uint8_t  type = 0;               // DataType (0 = normal DM); kept so a forwarded frame keeps its type
    uint8_t  inner[protocol::max_payload_bytes_hard_cap] = {};
    uint8_t  inner_len = 0;
    uint8_t  nonce_seed[8] = {};      // CRYPTED only: the 8-B nonce-seed from the DATA trailer (open at do_post_ack; preserved on forward)
    // Hop-budget for the forward (the decremented values from handle_data); copied
    // into the forward TxItem in do_post_ack.
    uint8_t  fwd_remaining = 0;
    uint8_t  fwd_committed = 0;
};
struct LastAcked { uint8_t chosen_data_sf = 0; uint64_t t_ms = 0; };
// Slice 3e.2: a learned gateway window schedule (from a heard gateway beacon's schedule_record block). The sender
// times its RTS to the gateway with gateway_schedule_defer_ms: visit_start = heard_ms + rec.offset_ms (NO shared
// wall clock — anchored to the heard instant); phase = (now - visit_start) mod period.
struct GatewaySchedule {
    bool     valid      = false;
    uint8_t  gw_node_id = 0;          // the gateway's node_id on the leaf we heard it = the RTS target
    uint64_t heard_ms   = 0;
    uint32_t period_ms  = 0;
    uint8_t  spread_nibble = 0;       // §3e herd-spread hint (0..15) advertised by the gateway; sizes the sender's herd-jitter
    uint8_t  n_rec      = 0;
    struct Rec { uint8_t leaf_id = 0; uint32_t window_ms = 0; uint32_t offset_ms = 0; } rec[2];
};

// Multi-hop gateway discovery (2026-06-14): one row = "gw_id (its node_id on THIS leaf) bridges TO dest_leaf". Fed by
// the type-4 BCN ext-TLV, re-gossiped by ALL nodes so the mapping travels the whole mesh; read by select_gateway_for_leaf
// so a node >1 hop from a gateway can still originate a cross-layer DM. Node-global (leaves originate). Last-write-wins.
struct BridgedLayer {
    uint8_t  gw_id        = 0;
    uint8_t  dest_leaf    = 0;
    uint64_t last_seen_ms = 0;
    bool     valid        = false;
};

// Slice 4c.1: a cross-layer DM the gateway must BRIDGE to its OTHER leaf — buffered (node-global, it spans leaves)
// until that leaf's window opens, then drained into the leaf's tx_queue (activate_layer). The re-inject `inner` is
// the ORIGINAL inner preserved verbatim (dst_hash + the cursor layer-path + origin + source_hash + body), with only
// the cursor byte advanced for a multi-gateway hop (v1 = last hop, unchanged). `dst_node_id` = the recipient resolved
// on the TARGET leaf's id_bind at bridge time. is_gw_relay marks it exempt from the originator anti-spam (wired 4c.2).
struct XlHandoff {
    bool     valid       = false;
    uint8_t  target_leaf = 0;        // the leaf INDEX (0/1) to re-inject on (its layer_id == layer_ids[cur])
    uint8_t  dst_node_id = 0;        // the recipient on the target leaf (the re-inject's routing dst); 0 = UNRESOLVED (4f: binding unknown at bridge -> resolve at drain)
    uint32_t dst_key_hash32 = 0;     // Slice 4f: the recipient's stable key -> re-resolve + H-flood the binding on the target leaf
    uint64_t last_h_flood_ms = 0;    // Slice 4f: throttle the unknown-binding H-reflood to one per visit period
    uint8_t  origin      = 0;        // the ORIGINAL sender (preserved end-to-end)
    uint16_t ctr         = 0;
    uint8_t  ctr_lo      = 0;
    uint8_t  flags       = 0;        // verbatim (CROSS_LAYER + E2E_ACK_REQ + DST_HASH + SOURCE_HASH ...)
    uint8_t  type        = 0;
    uint8_t  nonce_seed[8] = {};     // S1 (2026-07-04): CRYPTED only — the originator's 8-B nonce seed from the DATA trailer, kept verbatim so a cross-layer transit DM stays openable after the re-inject; zero for plaintext
    uint8_t  inner[protocol::max_payload_bytes_hard_cap] = {};
    uint8_t  inner_len   = 0;
    uint64_t queued_at_ms = 0;
};

}  // namespace MESHROUTE_NS
