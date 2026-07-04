// MeshRoute — lib/core/node.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The protocol node. Depends ONLY on hal.h (no Arduino/RadioLib/sol/json), so
// it runs unchanged on both HAL backends: FirmwareNode in the simulator and the
// MeshCore-PHY device backend. Bounded, fixed-size state (no heap in hot paths).
//
// SCOPE (as built, 2026-06): the Node spans the FULL same-layer + cross-layer stack —
// beacon emit + DV routing (aging/TTL prune/discovery FSM), the MAC data plane (RTS/CTS/
// DATA/ACK/NACK + throttle/triggered/cascade/LBT/NAV), hash-locate (H), E2E DM crypto
// (sealed-sender), node_id DAD + heal, channel gossip + flood, peer-liveness, and the
// dual-layer gateway. The class is split across partial-class TUs (node_*.cpp).
// The ONE unbuilt design slice is R6 leaf-config membership (lineage/epoch/config_hash
// beacon header + CONFIG_PULL) — docs/specs/2026-06-05-identity-leaf-membership-join-design.md
// + the R6 implementation spec. (Historical iteration docs: docs/specs/2026-05-29-r1-* / r2-*.)
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif
#include "hal.h"
#include "command.h"
#include "inbox.h"
#include "protocol_constants.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>
#include <optional>

namespace MESHROUTE_NS {

struct m_out;          // frame_codec.h — fwd-decl so the channel ingest seam doesn't pull the codec into node.h
struct rts_out;        // frame_codec.h — fwd-decl for the FLOOD RTS-M handler seam (handle_flood_rts)
struct SuspectEntry;   // frame_codec.h — fwd-decl for the §P4 suspect-gossip apply seam (apply_suspect_gossip)
struct beacon_entry;   // frame_codec.h — fwd-decl for the Slice 3 bidi detection scan seam (update_link_bidi_from_beacon)

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
    uint32_t beacon_period_ms  = 900000;
    uint32_t window_period_ms  = 15000;   // the full layer0->layer1 cycle (§3.2 default; cfg-overridable)
    uint32_t window_ms         = 0;       // this layer's presence in the cycle; 0 = DERIVE SF-weighted (§4)
    uint32_t window_offset_ms  = 0;       // phase; 0 = DERIVE anti-phase from the other layer (§4)
};

// §3.2 dual-layer validation result. The SHARED predicate validate_gateway_layers() returns this; on_init maps
// non-ok -> refuse (bool false), the `gateway` console command maps it to a specific operator error message.
enum class GwValErr : uint8_t {
    ok = 0, bad_leaf, bad_ctrl_sf, no_data_sf, leaf_nibble_clash,
    period_mismatch, period_zero, window_degenerate, window_zero, window_exceeds_period, window_overlap, window_too_long
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
};
struct PendingTx {                   // the in-flight sender state (one per node)
    uint8_t  origin = 0, dst = 0, next = 0, ctr_lo = 0;
    uint16_t ctr = 0;
    uint8_t  flags = 0;
    uint8_t  type = 0;               // DataType (0 = normal DM); carried into pack_data at do_data_tx
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

struct data_unicast_inner;   // frame_codec.h — fwd-decl for bridge_cross_layer's const-ref param (full type in node_mac_rx.cpp)

class Node {
public:
    Node(Hal& hal, uint8_t node_id, uint32_t key_hash32, const char* name = nullptr);

    bool on_init(const NodeConfig& cfg);                                 // cfg borrowed; false = REFUSED (bad dual-layer config, §3.2)
    // Reassign identity post-construct (device boots id=0 then loads it from NV; the join runtime sets it
    // too). 0 = unprovisioned (do_send is refused). 0xFF is reserved and ignored.
    void set_identity(uint8_t node_id, uint32_t key_hash32);
    // DP1 (Phase-1 E2E): install the X25519 ECDH secret + our Ed25519 pubkey, so we can seal/open DMs.
    // Identity is GLOBAL (a gateway shares ONE across both layers). Until set, crypto_ready()==false and any
    // seal/open FAILS LOUD (never silently falls back to cleartext). Backends derive these from the /mrid seed
    // (device) or the per-node scenario seed (sim).
    void set_crypto_identity(const uint8_t x_secret[32], const uint8_t ed_pub[32]);
    void on_recv(const uint8_t* bytes, size_t len, const RxMeta& meta);  // bytes valid during call only
    void on_timer(uint32_t timer_id);                                    // dispatch on Node-owned id
    void on_radio_busy(const BusyInfo& info);                            // deferred-TX retry/giveup
    void on_preamble_detected(uint64_t time_ms);                         // SX1262 IRQ / throttle witness
    CmdResult on_command(const Command& c);                              // the typed app<->firmware seam
    bool      next_push(Push& out);                                      // drain the async push ring (CMD_SYNC_NEXT)
    // Persistent inbox (durable DM + channel history). A backend installs durable stores via
    // inbox().on_init(dm, chan) AFTER Node::on_init; until then the inbox is disabled (record-on-delivery
    // is inert). The node records on its DM/channel deliver paths; a companion pulls incrementally.
    Inbox&    inbox() { return _inbox; }

    // OTA remote diagnostics (`rcmd`, 2026-06-24): a console-style query / response carried over a DATA DM
    // (DATA_TYPE_REMOTE_CMD / _RESP). lib/core is the GENERIC transport — fw_main owns the query whitelist + execution.
    struct RemoteInbound {                       // a received remote cmd/resp, staged for the main loop (NOT the inbox)
        bool    active = false;
        bool    is_response = false;             // true = a response to our cmd; false = a command for us to execute
        uint8_t from = 0;                        // the originator (pa.origin) — where the response goes back
        uint8_t len = 0;
        uint8_t body[protocol::inbox_max_body] = {};
    };
    uint16_t send_remote_cmd     (uint8_t dst, const uint8_t* body, uint8_t len);   // -> a DATA_TYPE_REMOTE_CMD DM (rides routing/ACK)
    uint16_t send_remote_response(uint8_t dst, const uint8_t* body, uint8_t len);   // -> a DATA_TYPE_REMOTE_RESP DM
    bool     take_remote_inbound(RemoteInbound& out);                              // drain the single inbound slot (fw_main, each loop)

    // Exposed for the R3.x determinism golden test. The retry-jitter RANGE is a
    // cross-engine alignment contract: 3*airtime_routing(RTS_LEN=8) must equal
    // the Lua's, or the lua-vs-meshroute forced-retry streams de-align (see the
    // node.cpp definition comment). Pure, const, no side effects.
    uint32_t  retry_jitter_ms() const;                                   // 3*airtime(routing, RTS_LEN=8)

    // R4.0 duty-cycle budget tier (route-free; from the rolling airtime window). Lua dv:3555-3571.
    // Public for the tier-table unit test (the emit only observes >=CRITICAL, so the HEALTHY/STRAINED
    // boundary needs a direct call). Pure, const.
    enum class BudgetTier : uint8_t { healthy = 0, strained = 1, critical = 2, exhausted = 3 };
    BudgetTier compute_budget_tier() const;                              // HEALTHY when duty_cycle<=0 (disabled)
    // id_bind binding provenance + trust. source = WHERE it came from; confidence = whether it may OVERWRITE
    // a conflicting binding. authoritative (self / owner-confirmed hash-bind) overwrites + wins the
    // dedup-by-hash; claimed (beacon / cached / snooped) refuses a same-id conflict.
    enum class IdBindSource : uint8_t { self = 0, bcn = 1, h_query = 2, h_relay = 3 };
    enum class IdBindConf   : uint8_t { claimed = 0, authoritative = 1 };
    // overheard < authoritative < pinned. pinned = a QR/manually-scanned key (E2E provisioning §1): the MITM-resistant
    // tier — NEVER overwritten by an on-air answer, NEVER LRU-evicted, NEVER aged out (NV-backed on device).
    enum class PeerKeyConf  : uint8_t { overheard = 0, authoritative = 1, pinned = 2 };
    // Why e2e_seal_inner returned 0 (the seal failed). Lets enqueue_data fail LOUD distinctly per cause instead of
    // treating every 0 as "no pubkey" (which floods a WANT_PUBKEY + drops the DM). no_pubkey is the ONLY case that
    // floods; the rest are local refusals (no_identity=R3, too_large=R2, bad_rng=R7, cross_layer=v1 scope).
    enum class SealOutcome  : uint8_t { ok = 0, no_identity, no_pubkey, too_large, bad_rng, cross_layer };
    bool       is_blind(uint8_t next_hop) const;                         // _blind_until active? (read-only; bounded by neighbour count)
    // ① mobile-as-transit avoidance (Lua dv:1325-1334): learn the is_mobile beacon bit; NEVER relay THROUGH a mobile
    // peer (it roams away), but DO deliver TO one (the next_hop==dest carve-out). Hard-exclude, not a score penalty.
    bool       is_mobile_peer(uint8_t id) const;
    bool       route_uses_mobile_as_transit(uint8_t dest, uint8_t next_hop) const;
    uint8_t    get_neighbor_tier(uint8_t node_id) const;                 // R4.2 tier read (TTL-expiring lazy-prune); public for tests
    void       schedule_triggered_beacon();                             // R4.3 trigger jitter + min-interval defer; public for tests
    int        mark_neighbor_budget_tier(uint8_t node_id, uint8_t tier, const char* source, bool local_only); // :4320; public for tests
    // R4.4 originator anti-spam (dv:3205-3277). track = ledger append (prune+dedup-first); compute = the
    // sliding-window metric. kind: 0=rts, 1=cts. Draw-free. Public for tests.
    void       track_originator_observation(uint8_t sender, uint8_t kind, uint8_t ctr_lo, uint32_t air);
    void       compute_originator_metric(uint8_t sender, int& apparent, uint32_t& total_air,
                                         uint8_t& rts, uint8_t& cts) const;

    // ---- device-console diagnostics: const LIVE reads consumed by fw_main's routes/cfg/status seam.
    uint8_t           node_id()        const { return _node_id; }
    // The FULL 8-bit layer_id of the ACTIVE leaf (a gateway alternates leaves on the window schedule). Public so the
    // device console (`debug on`) can announce which layer the gateway is currently LISTENING on. Single-layer: layers[0].layer_id.
    uint8_t           active_layer_id() const { return _cfg.layers[static_cast<size_t>(_active - &_layers[0])].layer_id; }
    uint32_t          key_hash32()     const { return _key_hash32; }
    bool              crypto_ready()   const { return _crypto_ready; }   // DP1: a crypto identity is installed
    const NodeConfig& config()         const { return _cfg; }
    NodeConfig&       mutable_config()       { return _cfg; }   // LIVE tweak of dynamically-read cfg (device `cfg set`):
                                                                // touch ONLY fields the MAC re-reads each use (sf_list/lbt/
                                                                // beacon/nav/hop_cap/leaf_id/gateway), NOT on_init-cached (duty).
    // Live `cfg set` of the radio knobs (control SF / BW / CR) — updates the config the MAC + airtime read,
    // WITHOUT re-initing the Node (routes / in-flight flight survive). LBT-derived delays are cached at
    // on_init and go stale on a live change, but LBT is off by default and needs a reboot to enable.
    void set_radio_cfg(uint8_t routing_sf, uint32_t bw_hz, uint8_t cr) {
        _cfg.routing_sf = routing_sf; _cfg.radio_bw_hz = bw_hz; _cfg.radio_cr = cr;
    }
    // R6.3 §2 (provisioning verbs / decision b): re-derive the duty-cycle budget after a LIVE duty change
    // (the join/create verbs, adopt_c_config, cfg-set duty) so enforcement applies without a reboot. Mirrors
    // the on_init derivation (dv:8497) — closes the known duty-live gap (the budget was on_init-cached only).
    void recompute_duty_budget() {
        _duty_cycle_budget_ms = (_cfg.duty_cycle > 0.0)
            ? static_cast<uint64_t>(_cfg.duty_cycle * _cfg.duty_cycle_window_ms) : 0;
    }
    // R6.3 provisioning verbs: reset BOTH the live epoch and the max-seen tracker together (a join/leave -> 0,
    // a create -> 1). Keeps _max_seen_epoch from leaking an old leaf's numbering into a fresh lineage's writes.
    void reset_leaf_epoch_state(uint16_t epoch) { _cfg.config_epoch = epoch; _max_seen_epoch = epoch; }
    // Reset the join FSM so a re-DAD actually RUNS on a reprovision (join/create verbs). set_identity(0) alone leaves
    // _joined set, and CmdKind::join is idempotent-once-joined -> the DAD never fires. Shared with forced_rejoin.
    void reset_join_for_reprovision();
    // Reprovision (join/create/leave verbs ONLY — NOT the heal): the old network's routes are stale. Drop ALL learned
    // routes / id-bindings / gateway schedules so the node starts the new network with a clean table. (The heal keeps
    // its routes — same network, only the id changed — so this is NOT in reset_join_for_reprovision.)
    void clear_routing_state();
    // `prep-restart` middle-tier reset: drop EVERY volatile/learned table (routes + channel buffer + liveness + pending
    // TX/RX + flood + digest/pull + dedup maps + parked/l2c/mediated) to a fresh-but-PROVISIONED state. KEEPS _cfg
    // (node_id/layer/sf_list/lineage), the crypto identity, and the DAD join — no re-join needed. (node.cpp)
    void clear_learned_state();
    // Set by a verb reprovision (do_dad); join_adopt consumes it to restart discovery ONCE the new id is stable, so the
    // fast-cadence beacons + the REQ_SYNC route-bootstrap go out under the adopted id (not the transient 0).
    void set_rediscover_pending(bool v) { _pending_rediscover = v; }
    void restart_discovery();    // re-enter discovery (fast beacon cadence + REQ_SYNC pull) to rebuild routes
    uint8_t           rt_count()       const { return _active->_rt_count; }
    const RtEntry&    rt_at(uint8_t i) const { return _active->_rt[i]; }   // 0..rt_count()-1; candidates[0] is the primary
    // Console testing aid: manually force / drop a route, to stress the routing algorithms with arbitrary or
    // inconsistent routes. route_inject returns true if the candidate took (rt_merge can reject if better candidates
    // already hold the K slots). route_remove drops a dest's whole entry.
    bool route_inject(uint8_t dest, uint8_t next_hop, uint8_t hops, int16_t score_q4) {
        RtCandidate c{}; c.next_hop = next_hop; c.hops = hops; c.score = score_q4;
        c.last_seen_ms = _hal.now(); c.learned_leaf = _cfg.leaf_id;
        rt_merge(dest, c);
        const RtEntry* e = rt_find(dest);
        if (e) for (uint8_t i = 0; i < e->n; ++i) if (e->candidates[i].next_hop == next_hop) return true;
        return false;
    }
    bool route_remove(uint8_t dest) {
        for (uint8_t i = 0; i < _active->_rt_count; ++i)
            if (_active->_rt[i].dest == dest) { rt_remove(i); return true; }
        return false;
    }
    int16_t peer_penalty_q4(uint8_t node_id) const { return liveness_penalty_q4(node_id); }   // liveness (suspect/silent/dead) penalty on a next-hop; routes dump shows effective = score - pen
    LinkBidi          link_bidi_state(uint8_t node_id) const { return static_cast<LinkBidi>(_active->_link_bidi[node_id]); }  // bidi plane read (test/status); unknown for any unprobed link
    uint64_t          link_bidi_confirmed_ms(uint8_t node_id) const { return _active->_link_bidi_confirmed_ms[node_id]; }    // last-confirmation ms (test/status); 0 = never confirmed
#ifdef MESHROUTE_NATIVE
    uint8_t           link_bidi_at(uint8_t node_id) const { return _active->_link_bidi[node_id]; }   // raw LinkBidi (test/white-box)
    void              test_update_link_bidi_from_beacon(uint8_t advertiser, const beacon_entry* e, uint8_t n, bool complete) { update_link_bidi_from_beacon(advertiser, e, n, complete); }  // white-box: drive the Slice-3 detection scan directly
    void              test_ingest_beacon(const uint8_t* bytes, size_t len, const RxMeta& meta) { ingest_beacon(bytes, len, meta); }  // white-box: drive ingest_beacon directly (Slice 3 end-to-end)
#endif
    // A heard 1-hop gateway's stored window schedule (nullptr if none known) + the ms to defer an RTS to its window.
    // For the `routes` console dump: surface a gateway route's unique state (period / per-leaf windows / heard-age).
    const GatewaySchedule* rt_gateway_schedule(uint8_t gw_node_id) const { return find_gw_schedule(gw_node_id); }
    uint32_t          rt_gateway_defer_ms(uint8_t gw_node_id) const       { return gateway_schedule_base_defer_ms(gw_node_id, nullptr); }  // base (no jitter) — stable display
    void              rt_resort_for_pick(uint8_t dest) { refresh_route_order(dest, "test_pick"); }   // test: force the pick-time re-sort (freshness/penalty applied)
    void              test_set_link_one_way(uint8_t next_hop) {                    // §bidi test: drive a one_way transition + its fan-out (mirrors the real Slice-3 detection)
        _active->_link_bidi[next_hop] = static_cast<uint8_t>(LinkBidi::one_way);
        resort_routes_for_neighbor_penalty(next_hop, "test_one_way", /*local_only=*/true);
    }
    void    note_link_confirmed(uint8_t next_hop);   // local bidi confirm (real CTS / complete-heard-set hit): set confirmed + stamp + fan out
    void    decay_link_bidi(uint8_t next_hop);   // confirmed + stale past bidi_confirm_ttl_ms -> unknown (MF6: NEVER -> one_way)
    void    set_link_bidi_for_test(uint8_t next_hop, LinkBidi v) { _active->_link_bidi[next_hop] = static_cast<uint8_t>(v); }  // test seam: seed a bidi state directly
    bool    candidate_degraded(const RtCandidate& c) const;   // LIVE: c.degraded_from_wire || _link_bidi[c.next_hop]==one_way (never a sticky cache, MF5/OI1)
    int16_t bidi_penalty_q4(uint8_t next_hop) const;          // §bidi: one_way next-hop -> bidi_penalty_one_way_q4, unknown/confirmed -> 0 (PURE; composed into effective_score at node_routing.cpp:100, SORT-only — never a next_hop_selectable gate)
    size_t            test_build_suspect_ext(uint8_t* out, size_t cap) { return build_suspect_ext(out, cap); }                 // §P4 test: drive the gossip encoder
    void              test_apply_suspect_gossip(const SuspectEntry* e, uint8_t n, uint8_t src) { apply_suspect_gossip(e, n, src); }   // §P4 test: drive the gossip apply
    void              test_emit_beacon(const char* kind) { emit_beacon(kind); }   // §5 census/advertise tests: drive a deterministic beacon (bypasses the throttle)
    bool              has_pending_tx() const { return _active->_pending_tx.has_value(); }
    bool              tx_queue_full()  const { return _active->_tx_queue_n >= kTxQueueCap; }   // enqueue_data SILENTLY drops when full -> callers (firmware scheduled-send) gate on this before originating
    uint64_t          nav_until_ms()   const { return _nav_until_ms; }  // NAV reservation deadline (0 = clear); test/status accessor
    // ---- channel-plane inspection (public, like rt_count) + the two seams tests drive directly ----
    uint16_t          channel_buffer_count() const { return _active->_channel_buffer_n; }
    bool              channel_has(uint32_t id) const { return channel_buffer_find(id) >= 0; }
    // ---- id_bind (hash-locate substrate) inspection: tests + the H resolver drive these.
    uint16_t          id_bind_count() const { return _active->_id_bind_n; }
    bool              joined()        const { return _joined; }        // DAD: adopted a node_id (test/app accessor)
    bool              in_discovery()  const { return _discovery_mode; } // fast-cadence route-bootstrap window (test/app accessor)
    // Duty-cycle consumption readout (console `duty` + companion). 0..100% of the rolling-window budget (100 = the node
    // must stay silent); avail_ms = ms until SOME airtime ages back in (0 when there's headroom); enabled=false = no
    // limit. Pure accessor — surfaces what duty_over_budget already computes; no state change.
    struct DutyStatus { uint8_t pct; uint32_t avail_ms; bool enabled; };
    DutyStatus        duty_status() const;
    // Anti-spam v2 (MF1/MF8): the channel-cap duty basis D = duty_cycle * originator_window_ms — a 5-MINUTE budget
    // (1% -> 3000 ms). Deliberately NOT _duty_cycle_budget_ms (a 1-HOUR budget, 12x too big for the 5-min cap window).
    // Returns 0 when duty is disabled (duty_cycle <= 0) — the sentinel the legacy-flat-cap fallback (MF2) keys on.
    uint32_t          channel_duty_budget_ms() const {
        return (_cfg.duty_cycle > 0.0)
            ? static_cast<uint32_t>(_cfg.duty_cycle * protocol::originator_window_ms)
            : 0u;
    }
    // Anti-spam v2 (2026-06-30): the SF/mesh/duty-aware per-origin CHANNEL cap (distinct floods/origin/window). Pure,
    // const, draw-free. MF2: duty disabled (channel_duty_budget_ms()==0) -> the legacy flat cap. Else MF1/MF3:
    // T_ch = airtime_routing_ms(43) + airtime_ms(max_data_sf(),...); C = max(1, D/T_ch); shared C/N_active among origins.
    uint16_t          channel_cap_origin() const;
    // `limits` query snapshot (companion anti-spam/headroom screen). Live-computed on demand: counters +
    // the channel_capacity_C()/channel_cap_origin() formula (cheap, idempotent, no state change). *_next_ms = the
    // true "when can I send next" = max(burst-floor remaining, channel window cap-wait, duty recovery).
    struct LimitsSnapshot {
        uint32_t win_ms, win_left_ms, n, ch_sf, ch_cap, ch_used,
                 ch_min_ms, ch_next_ms, ch_ceiling, dm_min_ms, dm_next_ms, duty_ms, duty_used_ms;
    };
    LimitsSnapshot    limits_snapshot() const;
    bool              key_hash_of_id(uint8_t id, uint32_t& out) const;  // id_bind reverse lookup (AUTHORITATIVE-only); false = unknown/claimed-only (DST_HASH omitted). Public for the send-path test.
    uint8_t           claim_epoch()   const { return _claim_epoch; }
    void              restore_join_state(uint8_t claim_epoch, bool joined) { _claim_epoch = claim_epoch; _joined = joined; }  // boot: reload persisted DAD state (NV)
    // Channel send-ctr persistence (metal reboot id-reuse fix): the self-keyed _peer_send_counter entry = the LAST
    // channel ctr this node minted. channel_ctr() reads it (0 if none); restore_channel_ctr seeds it at boot so the
    // first post-boot next_ctr(_node_id) CONTINUES (no re-mint of an already-used channel_msg_id). Call after on_init
    // (when _active + _node_id are valid). Host-testable.
    uint16_t          channel_ctr() const { auto it = _active->_peer_send_counter.find(_node_id); return it != _active->_peer_send_counter.end() ? it->second : 0; }
    void              restore_channel_ctr(uint16_t v) { _active->_peer_send_counter[_node_id] = v; }
    // D7 (companion-contract): generalize the channel_ctr lease to a per-peer high-water FLOOR — the DM dedup identity
    // (sender_hash, ctr) must not collide after a reboot re-mints ctrs. peer_ctr_high() = the MAX ctr across ALL
    // _peer_send_counter entries (the self/channel counter is just one of them); the lease persists THIS + margin.
    // restore_peer_ctr_floor seeds the boot floor so every per-peer next_ctr resumes ABOVE the pre-reboot high-water.
    uint16_t          peer_ctr_high() const { uint16_t m = 0; for (const auto& kv : _active->_peer_send_counter) if (kv.second > m) m = kv.second; return m; }
    void              restore_peer_ctr_floor(uint16_t v) { _active->_peer_ctr_floor = v; }
    uint16_t          test_next_ctr(uint8_t dst) { return next_ctr(dst); }   // D7 test seam: drive the (floor-applied) per-peer counter
    // §6 DAD tiebreak (pure): higher claim_epoch wins; tie -> lower key_hash32 wins. Public for the convergence test.
    static bool       join_tiebreak_wins(uint8_t my_epoch, uint32_t my_key, uint8_t their_epoch, uint32_t their_key);
    int               id_bind_find_by_hash(uint32_t key_hash32, IdBindConf* conf_out = nullptr);   // -> node_id, or -1 (skips expired); opt. out: the binding's confidence (soft/hard resolve)
    // E2E peer-pubkey cache (Phase 1 §6). Public for the seal/open paths + tests. hash-verified (ed_pub[:4]==hash),
    // authoritative-never-downgraded, evict-oldest at cap_peer_keys, TTL-aged. Per the ACTIVE layer.
    bool              peer_key_set(uint32_t key_hash32, const uint8_t ed_pub[32], PeerKeyConf conf);   // false: ed_pub[:4]!=hash
    bool              peer_key_find(uint32_t key_hash32, uint8_t ed_pub_out[32], PeerKeyConf* conf_out = nullptr);  // false: absent/aged
    void              peer_key_age_out();                                                              // drop entries past peer_key_ttl_ms
    uint16_t          peer_key_count() const { return _active->_peer_keys_n; }
    // E2E seal/open (Phase 1 §4/§5). Public for the send/receive paths + tests. SAME-LAYER DMs only in v1
    // (cross-layer CRYPTED out of scope). Recipient/sender pubkey resolved from the peer-key cache; ECDH+KDF+nonce
    // via _x_secret. e2e_seal_inner builds [dst_hash 4][ciphertext][tag 16] + the 8-B nonce-seed (§1c: pt = origin‖…); returns
    // inner_len, or 0 with `outcome` set to WHY (no_identity / no_pubkey / too_large / bad_rng / cross_layer) so the
    // caller FAILS LOUD distinctly, never cleartext. Only no_pubkey warrants a WANT_PUBKEY flood.
    size_t e2e_seal_inner(uint8_t* inner, size_t cap, uint8_t seed8[8], uint8_t flags, uint32_t dst_key_hash32,
                          uint8_t origin, uint16_t ctr, uint32_t source_hash, int32_t lat_e7, int32_t lon_e7,
                          const uint8_t* body, uint8_t body_len, SealOutcome& outcome);
    // e2e_open_inner: open under ONE candidate `sender_hash`; VERIFY the sealed source_hash == sender_hash. false = no
    // key / tag fail. `origin_out` = the DM's origin (1a: read from the cleartext AAD; 1c: recovered from the seal).
    bool   e2e_open_inner(const uint8_t* inner, size_t inner_len, const uint8_t seed8[8], uint8_t flags, uint16_t ctr,
                          uint32_t sender_hash, uint32_t& origin_out, uint32_t& source_hash_out, bool& has_location_out,
                          int32_t& lat_out, int32_t& lon_out, uint8_t* body_out, uint8_t& body_len_out);
    // §1a sealed-sender: TRIAL DECRYPTION — try each AUTHORITATIVE/PINNED cached peer key; the Poly1305 tag is the
    // oracle. First verifying key → decrypt + that key's owner IS the sender (sender_hash_out) + recover origin. false =
    // NO cached key opens it (caller DROPS silently — option a: no push/ack/inbox). No cleartext sender hint on the wire.
    bool   e2e_open_trial(const uint8_t* inner, size_t inner_len, const uint8_t seed8[8], uint8_t flags, uint16_t ctr,
                          uint32_t& sender_hash_out, uint32_t& origin_out, uint32_t& source_hash_out,
                          bool& has_location_out, int32_t& lat_out, int32_t& lon_out, uint8_t* body_out, uint8_t& body_len_out);
    void              on_hash_bind_response(const uint8_t* inner, uint8_t inner_len, bool authoritative);   // C.1: the origin consumed an H_ANSWER DATA -> cache (h_query) + drain. authoritative from the frame TYPE. public = the deliver seam + test driver
    void              on_hash_bind_snoop(const uint8_t* inner, uint8_t inner_len, bool authoritative);      // C.2: a forwarder snooped an H_ANSWER in transit -> cache-on-pass (h_relay). authoritative from the frame TYPE. public = the relay seam + test driver
    void              on_hash_bind_pubkey(const uint8_t* inner, uint8_t inner_len);   // E2E §6: a DATA TYPE 5 (delivered OR relayed-through) -> cache the ed_pub authoritative (verify ed_pub[:4]==hash)
    bool              channel_entry_dirty(uint32_t id) const { const int i = channel_buffer_find(id); return i >= 0 && _active->_channel_buffer[i].dirty; }
    bool              channel_payload_eq(uint32_t id, const uint8_t* p, uint16_t len) const {
        const int i = channel_buffer_find(id);
        if (i < 0 || _active->_channel_buffer[i].payload_len != len) return false;
        for (uint16_t k = 0; k < len; ++k) if (_active->_channel_buffer[i].payload[k] != p[k]) return false;
        return true;
    }
    static uint32_t   channel_msg_id_mint(uint8_t origin, uint32_t key_hash32, uint8_t ctr);   // origin<<24|(kh&0xffff)<<8|ctr (dv:2239)
    void    ingest_channel_m(const m_out& m, uint8_t from);  // M-frame merge (dv:10942); public for tests
    // Origin-level DATA dedup (loop/retransmit detection): record (origin,dst,ctr)->expiry + the prev-hop.
    // Prunes expired, then ROLLS (evicts the oldest = min-expiry) at the 256 cap instead of refusing. Public for tests.
    void    record_seen_origin(uint64_t sokey, uint8_t from, uint64_t now_ms);
    size_t  seen_origin_count() const { return _active->_seen_origins.size(); }
    bool    seen_origin_live(uint64_t sokey, uint64_t now_ms) const {
        auto it = _active->_seen_origins.find(sokey); return it != _active->_seen_origins.end() && it->second > now_ms; }

    // ---- Peer-liveness + freshness plane (routing-liveness port). Public for tests + the hooks. PHASE 1 = STATE
    // ONLY: tracked + emitted, NOT yet applied to scoring/selection/cascade (that is Phase 2/3). ----
    void    record_peer_rts_timeout(uint8_t node_id, uint8_t ctr_lo);   // a same-hop RTS/ACK giveup -> count + tier (suspect@2/silent@3/dead@6 over the evidence window)
    void    clear_peer_suspect(uint8_t node_id, const char* source);   // a frame heard FROM node_id -> it's alive -> clear its timeout state + tiers
    void    mark_dest_seen(uint8_t node_id);                           // stamp last-seen-as-transmitter (freshness input)
    uint8_t peer_suspect_level(uint8_t node_id);                       // 0 healthy / 1 suspect / 2 silent / 3 dead (clears expired tiers lazily)
    bool    is_next_hop_fresh(uint8_t node_id) const;                  // now - dest_seen <= next_hop_live_ttl_ms (self = always fresh); DEFINED, not consulted in P1

private:
    // Node-owned timer-id namespace (Hal::after re-arm-by-id, cap 64). Reserve
    // 4+ for the R3 RTS/CTS/ACK timers.
    static constexpr uint32_t kBeaconTimerId           = 1;
    static constexpr uint32_t kAgingTimerId            = 2;
    static constexpr uint32_t kTriggeredBeaconTimerId  = 3;
    // R3 data-plane (MAC) timers — single-flight per node, so one live instance each.
    static constexpr uint32_t kRtsTimeoutTimerId       = 4;   // sender: CTS-wait
    static constexpr uint32_t kAckTimeoutTimerId       = 5;   // sender: ACK-wait
    static constexpr uint32_t kPendingRxExpiryTimerId  = 6;   // receiver: DATA-wait
    static constexpr uint32_t kCtsToDataGapTimerId     = 7;   // sender: CTS-rx -> DATA-tx gap
    static constexpr uint32_t kQueueWakeupTimerId      = 8;   // become_free: not-ready re-arm
    static constexpr uint32_t kPostAckTimerId          = 9;   // receiver: ACK-air -> deliver/forward
    static constexpr uint32_t kRetryBackoffTimerId     = 10;  // sender: jittered RTS retry
    // Cascade-to-alt / no-route defer plane.
    static constexpr uint32_t kDeferredDrainTimerId    = 11;  // periodic 1s drain of _deferred (TTL giveup)
    static constexpr uint32_t kCascadeRequeueTimerId   = 12;  // backoff before re-draining a requeued flight
    static constexpr uint32_t kNackWaitTimerId         = 13;  // NACK BUSY_RX wait-same-hop one-shot
    static constexpr uint32_t kReqSyncTimerId          = 14;  // REQ_SYNC boot loop: re-arm every req_sync_retry_ms while discovery+route-starved (dv:9167)
    static constexpr uint32_t kBeaconJitterTimerId     = 27;  // R4.3 silence-jitter deferred periodic beacon; BASE of a 4-slot ring [27..30] (cleanup #D: two defers in one jitter window must BOTH fire, dv per-closure)
    static constexpr uint8_t  kBeaconJitterSlots       = 4;
    static constexpr uint32_t kRtsDutyDeferTimerId     = 31;  // cleanup #A redo: over-budget RTS duty-defer re-check/hand
    static constexpr uint32_t kLbtDeferTimerId         = 15;  // R4.5 LBT deferred-TX re-fire; BASE of a 4-slot range [15..18]
    static constexpr uint32_t kRadioBusyRetryTimerId   = 19;  // R4.5b on_radio_busy stash re-issue; BASE of a 4-slot range [19..22]
    static constexpr uint32_t kDutyDeferTimerId        = 23;  // tx_with_retry duty-cycle pre-check defer; BASE of a 4-slot range [23..26]
    static constexpr uint32_t kSyncResponseTimerId     = 32;  // REQ_SYNC jittered response fire; BASE of a kSyncRespSlots ring [32..47] (one slot per pending requester)
    static constexpr uint8_t  kSyncRespSlots           = protocol::cap_sync_response_pending;
    static constexpr uint32_t kChannelPullTimerId      = 48;  // channel CHANNEL_PULL jittered fire; BASE of a kChannelPullSlots ring [48..55]
    static constexpr uint8_t  kChannelPullSlots        = protocol::cap_channel_pull_pending;
    static constexpr uint32_t kMBcastClearTimerId      = 56;  // M-broadcast fire-and-forget: clear pending_tx after the DATA-M airtime (no ACK)
    static constexpr uint32_t kOverhearRetuneTimerId   = 57;  // overhear ARM: retune RX back to routing_sf after the DATA-M window
    static constexpr uint32_t kJoinClaimGuardTimerId   = 58;  // node_id DAD: claim guard window -> adopt-or-deny
    static constexpr uint32_t kJoinRetryTimerId        = 59;  // node_id DAD: jittered re-claim after a lost claim/heal
    static constexpr uint32_t kJoinListenTimerId       = 60;  // node_id DAD: listen window before the FIRST claim (L1: hear the leaf, then pick)
    static constexpr uint32_t kFloodRebcastTimerId     = 61;  // channel flood rebroadcast fire; BASE of a ring [61..63] (slot = id - base); LAST of the dense 1..63 block
    // ---- Slice 3 dual-layer gateway scheduler band [64..79] (kCap raised 64->80 in 3b). PER-LAYER timers: slot = layer
    // index (0..1). Inert on a single-layer node (n_layers==1 never arms them). The window open/close + per-leaf beacon
    // are the only PERSISTENT per-layer timers; the MAC exchange timers (RTS/ACK/retry rings) are active-layer-shared.
    static constexpr uint32_t kLayerWindowTimerId      = 64;  // per-layer window-OPEN / leaf-switch fire; BASE of [64..65]
    static constexpr uint32_t kLayerWindowCloseTimerId = 66;  // per-layer window-CLOSE / return fire;      BASE of [66..67]
    static constexpr uint32_t kLayerBeaconTimerId      = 68;  // per-leaf beacon cadence (gateway);         BASE of [68..69]
    // The gateway band [64..69] and the channel re-offer ring [70..73] are ROLE-EXCLUSIVE: a gateway (n_layers==2)
    // is out of the channel provider plane (§7 — never originates a flood, so never arms re-offer), and a normal node
    // (n_layers==1) never arms the gateway timers. So they coexist within kCap=80 with no overlap; the after() id<kCap
    // bound stays intact (the canary era: the wheel is exonerated only because it bounds).
    static constexpr uint32_t kChannelReofferTimerId   = 70;  // channel ORIGIN re-offer jittered fire; BASE of a ring [70..73] (slot = id - base)
    static constexpr uint8_t  kChannelReofferSlots     = protocol::cap_channel_reoffer_pending;
    // [74..79] free for future per-layer timers.

    // ---- beacon emit / ingest ----------------------------------------------
    void emit_beacon(const char* kind);                            // "periodic" | "triggered"
    void periodic_beacon_fire();                                   // R4.3 throttle body (dv:7695-7851)
    void deferred_beacon_jitter_fire(uint8_t slot);                // R4.3 post-silence-jitter re-check (dv:7801-7849); #D ring slot
    bool _beacon_jitter_pending[kBeaconJitterSlots] = {};          // #D: which ring slots have a deferred periodic beacon armed
    bool beacon_max_idle_force(uint64_t now, bool emit_events);    // R4.3 max-idle B+C override (dv:7734-7784)
    void ingest_beacon(const uint8_t* bytes, size_t len, const RxMeta& meta);
    uint16_t cfg_config_hash() const;                             // R6.1: leaf_config_hash over THIS node's active cfg (u16)
    // R6.1 §6.4 join-participation gate: a node may originate F/DATA only once its leaf config is "synced" — UNMANAGED
    // (lineage 0, backward-compat, always allowed) OR managed-and-adopted (config_epoch > 0; leaf-create/CONFIG_PULL set it).
    // An un-synced managed joiner must listen + CONFIG_PULL only (no F/DATA pollution before it's a member).
    bool     leaf_config_synced() const { return _cfg.lineage_id == 0 || _cfg.config_epoch > 0; }
    int16_t route_score_from_snr(int16_t snr_q4) const;            // dv_dual_sf.lua:3053
    // Direct (hops=1) neighbour learning from a received frame's immediate sender — the C++
    // learn_rx_source / learn_direct_from_frame. Returns true on a real change (new/promote/
    // refresh) so the caller can fire the triggered beacon. sender must be a real id (0..254);
    // 0xFF (unknown/reserved) and self are no-ops. C++ has no id-bind/dest-seen/liveness plane,
    // so (unlike the Lua) those sub-actions are absent.
    bool    learn_direct_neighbor(uint8_t sender, int16_t snr_q4, bool is_gw);
    void    learn_route_via(uint8_t dest, uint8_t via, uint8_t hops, int16_t snr_q4);  // multi-hop install (F path)
    // F route discovery (AODV RREQ/RREP) — node_route_discovery.cpp.
    void    handle_f(const uint8_t* bytes, size_t len, const RxMeta& meta);
    void    emit_route_request(uint8_t dst, uint8_t ttl);
    void    send_route_reply(uint8_t origin, uint8_t dst, uint8_t hops_to_dst);
    bool    rreq_seen_recently(uint8_t origin, uint8_t dst);
    void    mark_rreq_seen(uint8_t origin, uint8_t dst);
    bool    rreq_rate_ok(uint8_t dst, uint8_t ttl);
    // Hash-locate (H) plane — node_hashlocate.cpp. id_bind = the key_hash32->node_id binding table, the
    // substrate the H resolver answers from (Lua dv:4677+). Populated by beacons (every BCN carries the
    // sender's key_hash32) + self + hash-bind responses.
    bool    id_bind_set(uint8_t node_id, uint32_t key_hash32, IdBindSource source, IdBindConf confidence); // insert/update; dedup-by-hash; authoritative overwrites a conflict, claimed refuses
    uint8_t id_bind_evict_other_hash_holders(uint32_t key_hash32, uint8_t keep_node_id);   // rejoin self-heal: one hash -> one node_id
    void    id_bind_age_out();                                    // drop expired (TTL); emit id_bind_aged
    void    handle_h(const uint8_t* bytes, size_t len, const RxMeta& meta);   // H flood: resolve (own-hash OR id_bind) + suppress, else forward TTL-1
    bool    hash_query_seen_recently(uint8_t origin, uint32_t key_hash32, bool hard, bool want_pubkey);   // per-(origin,hash,VARIANT) dedup; VARIANT = hard + want_pubkey (§2: a WANT_PUBKEY isn't suppressed by a prior plain HARD)
    void    mark_hash_query_seen(uint8_t origin, uint32_t key_hash32, bool hard, bool want_pubkey);
    void    send_hash_bind_response(uint8_t to_origin, uint8_t target_layer, uint8_t node_id, uint32_t key_hash32, bool authoritative); // B: routed DATA(H_ANSWER inner) home
    void    send_hash_bind_pubkey_response(uint8_t to_origin, uint8_t target_layer, uint8_t node_id, const uint8_t ed_pub[32]);  // E2E §6: routed DATA TYPE 5 (the owner's ed_pub)
    // D — send-by-hash trigger (the deferred "address by key_hash32") + verify-on-use.
    uint16_t send_by_hash(uint32_t key_hash32, const uint8_t* body, uint8_t body_len, uint8_t flags, CryptIntent crypt = CryptIntent::def); // authoritative binding -> send now; soft/unknown -> park + flood (soft binding -> HARD verify)
    void    emit_hash_query(uint32_t key_hash32, bool hard, bool want_pubkey = false);   // H flood for key_hash32 (hard = verify-on-use; want_pubkey = E2E §6, ask the owner's ed_pub)
    void    park_send(uint32_t key_hash32, const uint8_t* body, uint8_t body_len, uint8_t flags);
    void    park_send_layer(uint32_t key_hash32, const uint8_t* body, uint8_t body_len, uint8_t flags);   // Slice 4d: a cross-layer-capable park (resolves layer + gateway on the H-answer); flags carry the app's E2E_ACK_REQ etc.
    void    drain_parked_sends(uint32_t key_hash32, uint8_t resolved_id, uint8_t target_layer = 0xFF);   // a binding arrived -> fly the parked DMs to it (target_layer from the H-answer, 0xFF = beacon re-drain / unknown)
    // Slice 4d: cross-layer origination — select a bridging gateway (schedule-verified) + build the CROSS_LAYER DM.
    uint8_t select_gateway_for_leaf(uint8_t target_leaf);        // a gateway (1-hop schedule OR multi-hop _bridged_layers) bridging to target_leaf; 0 = none. Two-pass: routed-preferred, then unrouted fallback (non-const: prunes aged rows)
    void    send_cross_layer(uint8_t dst_node, uint32_t dst_hash, uint8_t target_layer, const uint8_t* body, uint8_t body_len, uint8_t flags);  // pick G + enqueue, else err_no_gateway (4d.2: park+ROUTE_QUERY); flags honored on the DM
    // Explicit-path origination (console/companion send_layer, §5): route a cross-layer DM along the user-supplied
    // layer path [our_layer, hops...] cur=1, NO H-query. Returns SYNCHRONOUSLY (no orphan push): CmdCode::queued (+
    // out_ctr = the MAC ctr the app correlates async pushes by), err_no_gateway (no gateway serves hops[0]'s leaf),
    // or err_too_large (the inner overflows). The handler validates the path before calling (hop_count, layer!=0, hops[0]!=ours).
    CmdCode originate_layer_path(uint32_t dst_hash, const uint8_t* hops, uint8_t hop_count, const uint8_t* body, uint8_t body_len, uint8_t flags, uint16_t& out_ctr);
    bool    enqueue_cross_layer(uint8_t gw_node, uint32_t dst_hash, const uint8_t* layer_ids, uint8_t n_layers, uint8_t cur, const uint8_t* body, uint8_t body_len, uint8_t flags, uint16_t* out_ctr = nullptr);  // build the layer-path inner -> next-hop G; honors flags & E2E_ACK_REQ; *out_ctr=ctr on success; false = fit/queue fail
    void    drain_resolved_parked_sends();                       // beacon-tick re-drain: any parked hash now authoritatively bound
    void    age_out_parked_sends();                              // give up on parked sends past send_defer_ttl_ms
    // Diagnostic `resolve` (CmdKind::resolve): locate a hash WITHOUT sending a DM. Authoritative cache hit (or
    // own hash) -> answer now; else park a notify-only request + flood H. The answer/timeout rides hash_resolved.
    void    request_resolve(uint32_t key_hash32, bool hard);     // the resolve entrypoint (called by on_command)
    void    park_resolve_request(uint32_t key_hash32);           // park a notify-only resolve (de-dups by hash)
    void    push_hash_resolved(uint32_t key_hash32, uint8_t node_id, bool authoritative);  // enqueue the push (node_id 0 = timeout)
    // L2c verify-on-delivery: a DM whose DST_HASH != our key was misdelivered by an id collision —
    // FORWARD it (identity-preserving, not re-originated) toward the real owner of want_hash. The HEAL
    // (renumber) is confirmation-gated: deferred to the HARD-H resolution, fired only when want_hash resolves
    // back to OUR own id (a proven same-id collision) — see design §7.1.
    void    l2c_handle_misdelivery(const PostAck& pa, uint32_t want_hash);
    void    l2c_park_redirect(uint32_t want_hash, const PostAck& pa);                 // hold a misdelivered DM for forward-on-resolution
    bool    l2c_enqueue_forward(uint8_t to_id, uint8_t origin, uint16_t ctr, uint8_t ctr_lo, uint8_t flags,
                                uint8_t type, const uint8_t* inner, uint8_t inner_len, const uint8_t nonce_seed[8]);   // fresh ORIGINATOR-budget leg; type/nonce_seed threaded (S1: a typed/CRYPTED redirect keeps them); false = dropped (queue full)
    void    l2c_confirmed_collision(uint32_t want_hash);                              // HARD-H resolved want_hash->our id => key-only heal (called AFTER the drain loop)
    bool    l2c_redirected_recently(uint32_t want_hash);         // one redirect action per hash per window (anti-flood)
    void    l2c_mark_redirected(uint32_t want_hash);
    // node_id auto-assignment (DAD + heal) — node_join.cpp.
    int     join_choose_candidate_id();                          // prefer previous id, else a random free slot (-1 = leaf full)
    bool    join_start_claim(const char* reason);                // pick a candidate, bump epoch, broadcast J_CLAIM, arm the guard
    void    join_claim_guard_fire();                             // kJoinClaimGuardTimerId: adopt (no objection) or deny+retry
    void    join_adopt(uint8_t node_id);                         // set_identity + joined + self-bind + beacon
    void    handle_j(const uint8_t* bytes, size_t len, const RxMeta& meta);   // J RX dispatch (CLAIM/DENY; DISCOVER/OFFER later)
    void    addr_conflict_send_deny(uint8_t node_id, uint32_t owner_key, uint32_t claimant_key, uint8_t reason);  // owner defends its id
    void    forced_rejoin(const char* reason);                   // lost the heal tiebreak -> yield id + re-claim
    void    join_deny_id(uint8_t id);                            // add to the denied list (1-day TTL)
    bool    join_id_denied(uint8_t id) const;                    // is this id currently denied (not expired)?
    void    age_out_denied_ids();                                // drop denied entries past dad_denied_id_ttl_ms
    bool    mediated_recently(uint8_t node_id, uint32_t loser_hash) const;  // L2a: did we already DENY this (id,loser) this window?
    void    mark_mediated(uint8_t node_id, uint32_t loser_hash);            // L2a: record a sent mediated DENY
    void    age_out_mediated();                                             // drop mediation records past the suppress window
    // Q REQ_SYNC plane (boot route-bootstrap) — node_query.cpp.
    void    req_sync_loop_fire();                                  // kReqSyncTimerId: send + re-arm while discovery+starved (dv:9167)
    void    send_req_sync_q(const char* reason, bool force = false);  // broadcast a REQ_SYNC Q (no draw; dv:8032). force=bypass boot-flag+route-rich guards (reactive route-miss pull)
    void    send_config_pull(uint8_t to, uint16_t lineage, uint16_t epoch);  // R6.2: 1-hop CONFIG_PULL to a heard member
    void    send_c_config(uint8_t to);                            // C frame (cmd 0xB): control-plane answer to a CONFIG_PULL carrying OUR leaf config (an empty-sf_list joiner CAN receive it)
    void    handle_c(const uint8_t* bytes, size_t len, const RxMeta& meta);   // C RX dispatch (cmd 0xB) -> adopt if addressed to us on our leaf
    void    adopt_c_config(const uint8_t* body, size_t len);      // adopt a pulled config (cfg + recompute + persist Push); lineage from the last-heard beacon
public:
    bool    leaf_config_write();                                   // R6.3 §4.1: operator config write -> epoch=max_seen+1 + re-advertise (managed only)
private:
    void    handle_q(const uint8_t* bytes, size_t len, const RxMeta& meta);   // Q RX dispatch (dv:11767)
    void    schedule_sync_response(uint8_t requester, bool requester_mobile); // jittered full-table reply (the backoff DRAW; dv:8064)
    void    sync_response_fire(uint8_t slot);                      // kSyncResponseTimerId+slot: emit_beacon("sync") unless suppressed
    bool    q_responded_recently(uint8_t opcode, uint8_t src, uint8_t dest);  // q_responded_to dedup (ttl q_respond_ttl_ms)
    void    mark_q_responded(uint8_t opcode, uint8_t src, uint8_t dest);      // refresh/append (evict-oldest; dv:11778)
    // Channel-message gossip plane (ROADMAP §3) — node_channel.cpp. Phase 1: buffer + origination + DATA-M ingest.
    // Single-layer; gateways skip (Principle 11). seen_by is a 256-bit bitmap (neighbour id -> bit) so the
    // safe-eviction cover-check is O(neighbours). Struct defs here so they precede both the decls + the state.
    struct ChannelEntry {
        uint32_t id;                 // channel_msg_id: origin<<24 | (key_hash32&0xffff)<<8 | ctr
        uint8_t  channel_id;
        uint8_t  flavor;
        uint8_t  origin;             // == (id >> 24): the minting node
        bool     dirty;              // advertise in the BCN digest until bcn_ad_count hits K (Phase 2)
        uint8_t  bcn_ad_count;
        uint64_t received_at;
        uint8_t  seen_by[32];        // 256-bit set of neighbours known to hold this msg (eviction safety)
        uint16_t payload_len;
        uint8_t  payload[protocol::channel_msg_max_payload_bytes];
    };
public:
    // Public so native tests can inspect the per-origin channel ledger directly (like channel_buffer_count()).
    struct ChannelOriginEvent  { uint32_t id; uint64_t t_ms; };
    struct ChannelOriginLedger {
        ChannelOriginEvent ev[protocol::cap_channel_origin_events];   // MF7: sized by the new const
        uint8_t  n = 0;
        uint64_t last_flood_ms = 0;   // Slice 2: per-origin last admitted flood — the channel_min_interval_ms burst floor
    };
private:
    // Channel FLOOD in-progress state (2026-06-08 redesign). One slot per concurrent flood mid-backoff;
    // slot i owns rebroadcast timer kFloodRebcastTimerId+i. active while awaiting_data (overhear) OR while
    // its rebroadcast timer is armed; freed on fire / coverage-cancel / no-unmarked / anti-spam drop.
    struct FloodState {
        bool     active = false;
        bool     awaiting_data = false;   // RTS-M seen, DATA-M not yet (fast-self-pull candidate)
        uint32_t id = 0;                  // channel_msg_id
        uint8_t  src = 0;                 // who relayed it to us (pull target / neighbour-learn)
        int16_t  rx_snr_q4 = 0;           // SNR of the winning RTS-M (drives the backoff)
        uint8_t  bitmap[32] = {};         // working coverage (OR'd from every heard RTS-M for this id)
        uint8_t  body[protocol::channel_msg_max_payload_bytes] = {};  // cached for the re-flood DATA-M
        uint8_t  body_len = 0, channel_id = 0, flavor = 0, hop_left = 0;
    };
    struct ChannelPullPending  { bool active; uint32_t id; uint8_t target; uint64_t requested_at; uint64_t fire_at; };
    struct ChannelPullRecent   { uint32_t id; uint64_t t_ms; };    // re-pull dedup (Lua channel_pull_recent)
    struct ChannelReofferPending { bool active; uint32_t id; uint8_t retries_left; };   // Part 2: per-origin re-offer (timer kChannelReofferTimerId+slot)
    void    channel_reoffer_register(uint32_t id);                 // Part 2: arm a re-offer slot on flood origination (retries_left=channel_reoffer_max_retries)
    void    channel_reoffer_fire(uint8_t slot);                    // Part 2: timer fire — re-flood if not yet confirmed + retries remain, else free
    void    channel_reoffer_confirm(uint32_t id);                  // Part 2: a relay of OUR message was overheard -> cancel its pending re-offer (dedicated signal, NOT seen_by)
    int     channel_buffer_find(uint32_t id) const;                // index of the entry, or -1 (dv:3426)
    bool    channel_mark_seen_by(uint32_t id, uint8_t neighbour);  // set seen_by bit; true if newly set (dv:3434)
public:
    bool    channel_origin_admit(uint8_t origin, uint32_t msg_id); // per-origin distinct-count anti-spam (dv:3456). Public: the receiver-HOOK test seam (drives the cap + 10s burst floor directly).
    // Slice 6: the send-outcome feedback pushes. Public so native tests can drive them (the reoffer-exhaustion path
    // enqueues channel_sent{relayed:false}); called internally from do_send_channel / become_free / channel_reoffer_*.
    void    emit_send_blocked(bool channel, SendFailReason reason, uint32_t next_ms);   // Slice 6a: the send_blocked push (self-gate)
    void    emit_channel_sent(bool relayed, uint16_t ctr);                              // Slice 6c: OWN channel post re-offer outcome
private:
    int     channel_buffer_pick_eviction(bool* safe) const;        // oldest-all-seen else oldest; index (dv:3485)
    bool    channel_entry_fully_seen(const ChannelEntry& e) const; // 2026-06-23: every live 1-hop neighbour holds e (or none to serve) -> retire-OK (holder-aware retirement; NOT shared with pick_eviction — opposite nn==0 meaning)
    void    channel_buffer_add(const ChannelEntry& e);             // insert; evict if full (dv:3511)
    void    cancel_channel_pull(uint32_t id, uint8_t overheard_from, bool peer_q = false); // pull cancel: peer_q=true -> a peer's Q pulled it (dv:11831); else we received it (dv:11006)
    uint16_t do_send_channel(uint8_t channel_id, const uint8_t* body, uint8_t body_len);  // send_channel origination (dv:12126)
    // Phase 2: digest emit/ingest + the jittered pull (THE draw). SELECT/COMMIT split (B, 2026-06-23): build is side-effect-free
    // (fills `picked`); the per-ad ad_count++/retire is COMMITTED by emit_beacon ONLY when the beacon actually aired (tx_flood sent).
    size_t  build_channel_digest_ext(uint8_t* out, size_t cap, uint32_t* picked, uint8_t& npicked);  // SELECT: dirty ids -> BCN ext-TLV; NO side effects (dv:1426)
    void    commit_channel_digest_advertised(const uint32_t* ids, uint8_t n);  // COMMIT (on air): ad_count++ + holder-aware retire
    void    process_channel_digest(uint8_t src, const uint32_t* ids, uint8_t count);  // diff -> mark/schedule pull (dv:3546)
    void    channel_pull_fire(uint8_t slot);                       // kChannelPullTimerId+slot: re-check overhear -> tx the pull
    bool    channel_pull_recently(uint32_t id) const;             // re-pull dedup window (dv:3567)
    void    channel_pull_mark(uint32_t id);                        // record a fired pull (channel_pull_recent)
    // Phase 2c: the CHANNEL_PULL responder + M-broadcast tx.
    void    handle_channel_pull(uint8_t src, uint8_t dest, const uint32_t* ids, uint8_t count);  // dv:11821
    void    enqueue_channel_m(uint8_t target, const ChannelEntry& e);  // M-inner DATA -> tx_queue (dv:11875)
    bool    channel_m_in_flight(uint32_t id) const;              // an M-payload for `id` already pending/queued (dv:11850 dedup)
    bool    channel_have_id_lo16(uint16_t lo) const;             // do we hold a channel msg whose id low-16 == lo? (overhear skip, dv:2081)
    // M-broadcast fire-and-forget tx (no CTS/ACK; chosen_data_sf = max allowed; dv:6997/7044).
    void    issue_m_broadcast();                                  // set up the m_broadcast flight from _pending_tx + fire the RTS
    void    tx_m_broadcast_rts();                                 // pack+tx the M_BROADCAST (or FLOOD) RTS + arm the RTS->DATA gap (no CTS wait)
    // ---- channel FLOOD plane (2026-06-08 redesign; node_channel.cpp) -------------------------------
    int     flood_state_find(uint32_t id);                        // active slot for id, or -1
    int     flood_state_alloc(uint32_t id);                       // free slot, or -1 (all active -> DROP to repair; never evict, §6)
    void    flood_state_free(uint8_t slot);                       // clear active + cancel its rebroadcast timer
    void    flood_set_my_coverage(uint8_t* bm) const;            // set my bit + my hops==1 neighbour bits (originate-seed AND rebroadcast cover; idempotent)
    bool    flood_any_unmarked(const uint8_t* bm) const;         // true if any hops==1 neighbour is unmarked in bm
    void    enqueue_flood_m(uint8_t channel_id, uint8_t flavor, uint32_t id, const uint8_t* body, uint8_t body_len,
                            const uint8_t* bitmap32, uint8_t hop_left);   // build+enqueue a FLOOD m-broadcast (no target)
    bool    handle_flood_rts(const rts_out& r, const uint8_t* in_bitmap, int16_t snr_q4);  // §4.2 RX of a FLOOD RTS-M; true = fresh state -> retune to catch DATA-M
    void    flood_forward_decision(uint8_t slot);                // §4.5 after DATA-M ingest: silent | arm backoff
    void    flood_rebroadcast_fire(uint8_t slot);                // kFloodRebcastTimerId+slot: re-flood {unmarked+me}, hop_left--
    // void    flood_log_coverage(const char* tag, uint32_t id, const uint8_t* bm) const;  // FLOOD-DBG disabled 2026-06-23 (def #if 0'd in node_channel.cpp; re-enable for bench diag)
    void    flood_fast_self_pull(uint8_t slot);                  // §4.4: caught RTS-M, missed DATA-M -> pull from src
    uint8_t max_data_sf() const;                                  // highest SF in allowed_sf_bitmap (largest = most robust)
    uint8_t max_data_sf_index() const;                            // its index in the ascending allowed set (the RTS sf_index)
    static uint32_t m_inner_id(const uint8_t* inner);             // channel_msg_id (BE) from an M-inner buffer [id4|ch|fl|body]

    // ---- route table (DV merge) --------------------------------------------
    enum class MergeAction : uint8_t { none, new_dest, primary_refresh, promote, alt_install };
    RtEntry*    rt_find(uint8_t dest);
    RtEntry*    rt_insert(uint8_t dest);                           // sorted insert; nullptr if full
    void        rt_remove(uint8_t idx);                            // R2: drop _rt[idx], keep sort
    MergeAction rt_merge(uint8_t dest, const RtCandidate& cand);   // dv_dual_sf.lua:4484
    void        sort_candidates(RtEntry& e);
    // route_strictly_better/effective_score take the candidate LIST (cands,n) as context so the
    // R4.2 budget penalty can count viable alternatives (Lua signature (a,b,viab,candidates)). The
    // penalty is 0 for every HEALTHY-tier next_hop, so effective_score == score until a tier is marked.
    bool        route_strictly_better(const RtCandidate& a, const RtCandidate& b,
                                      const RtCandidate* cands, uint8_t n, bool gw_dest = false) const;  // :4227 (gw_dest: cross-layer freshness-exempt)
    bool        is_gateway_dest(uint8_t dest) const;          // §cross-layer: dest is a gateway egress (freshness-exempt)
    int16_t     effective_score(const RtCandidate& c, const RtCandidate* cands, uint8_t n) const; // :4050
    int16_t     budget_penalty_q4(const RtCandidate& c, const RtCandidate* cands, uint8_t n) const; // :3887
    int16_t     liveness_penalty_q4(uint8_t next_hop) const;       // §P2: suspect 192 / silent 640 / dead 1280 Q4 (const, non-mutating tier read); Lua peer_suspect_penalty_db@4008
    // Slice 3: the bidirectionality DETECTION scan. For advertiser P's beacon heard-set (its hops==1 entries),
    // a [dest==self] entry proves P hears us -> confirmed (note_link_confirmed); an ABSENT self in a COMPLETE
    // page proves P does NOT hear us -> one_way; an absent self in a TRUNCATED page is unconfirmed (no change).
    void        update_link_bidi_from_beacon(uint8_t advertiser, const beacon_entry* entries, uint8_t n, bool complete);
    int         resort_routes_for_neighbor_penalty(uint8_t node_id, const char* source, bool local_only);      // :4255
    RtEntry*    refresh_route_order(uint8_t dst, const char* reason);   // re-sort ONE dest's candidates (catch a tier change since the last sort), dv:4455
    void        maybe_emit_rt_full();

    // ---- R2 route-plane hardening ------------------------------------------
    void     age_out_stale_routes();                               // dv_dual_sf.lua:5249
    uint32_t ttl_for_hops(uint8_t hops) const;                     // hops<=1 neighbor else remote
    void     rt_prune_cycle(uint8_t dest, uint8_t sender);         // 3-cycle prune  :5193
    // ---- Peer-liveness internals (routing-liveness port) -------------------
    struct PeerLiveness;                                              // fwd decl (full def below, near the LayerRuntime member structs)
    PeerLiveness* peer_liveness_slot(uint8_t node_id, bool create);   // find (or LRU-create) the per-node slot; nullptr if absent + !create
    bool          e2e_ack_spoofer_flagged(uint8_t src);               // anti-spoof: has `src` been caught faking RTS_FLAG_E2E_ACK within the penalty window? (its exemption is then revoked). Non-const: peer_liveness_slot is non-const.
    void          mark_peer_suspect(uint8_t node_id, uint8_t level, const char* source, uint8_t remote_src = 0);   // set the tier expiry + resort (§P4: remote_src!=0 => gossip-learned: local_only resort + NO advertise-table write; remote_src is also echoed in the event)
    size_t        build_suspect_ext(uint8_t* out, size_t cap);    // §P4: locally-observed suspect/dead peers -> a type-1 or type-2 BCN ext-TLV; 0 = none (dv:1373 build_suspect_nodes_ext)
    void          apply_suspect_gossip(const SuspectEntry* e, uint8_t n, uint8_t bcn_src);   // §P4: a received suspect-TLV -> mark_peer_suspect(remote); skip self + the gossiper (dv:9627)
    void     maybe_exit_discovery(const char* reason);            // :7517

    // ---- R3 data plane (MAC: RTS-CTS-DATA-ACK) -----------------------------
    uint16_t do_send(uint8_t dst, const uint8_t* body, uint8_t body_len, uint8_t flags, CryptIntent crypt = CryptIntent::def);  // returns the ctr
    uint16_t enqueue_data(uint8_t dst, const uint8_t* body, uint8_t body_len, uint8_t flags, const char* tx_event,
                          bool app_dm = false, uint8_t type = 0, CryptIntent crypt = CryptIntent::def);
    void     send_e2e_ack(uint8_t to_origin, uint16_t acked_ctr);          // E2E ACK reply (TYPE=E2E_ACK; e2e_ack_tx)
    void     send_e2e_ack_cross_layer(const data_unicast_inner& dm, uint16_t acked_ctr);  // Slice 4e: reversed-path CROSS_LAYER E2E ack back to the original sender
    void     enqueue_push(const Push& p);                                  // append to the bounded ring
    void     become_free();                                       // dv_dual_sf.lua:7433 (FIFO single-drain)
    void     issue_send(const TxItem& item);                      // :7018 pending_tx + RTS
    void     clear_nack_wait() { _hal.cancel(kNackWaitTimerId); _nack_wait_pending = false; }   // drop a stale BUSY_RX wait
    void     handle_rts (const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'R' -> CTS
    void     handle_cts (const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'C' -> DATA
    void     handle_data(const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'D' -> deliver/forward + ACK
    void     handle_channel_data(const uint8_t* b, size_t n, const RxMeta& m);  // on_recv 'M' (cmd 0xA) -> leaf gate + ingest
    void     handle_ack (const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'K' -> done
    void     handle_nack(const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'N' -> blind+wait / cascade
    void     do_data_tx();                                        // kCtsToDataGapTimerId fire
    void     do_post_ack();                                       // kPostAckTimerId fire (deliver|forward)
    // ---- Slice 3 dual-layer gateway: leaf activation (3d's window scheduler drives activate_layer on timers,
    // gated by layer_swap_blocked). Retunes SF + per-leaf identity + the active-layer scalars/SNR-floor/LBT timing;
    // migrates the per-leaf sync-response ring (shared timer ids) off the LEAVING leaf so a stale fire can't hit it.
    void     activate_layer(uint8_t i);
    bool     layer_swap_blocked() const;                          // §4 busy-guard: never switch mid-exchange
    int16_t  routing_snr_floor_for(uint8_t routing_sf) const;     // SF_DEMOD_THRESHOLD[sf] + sf_margin (per-leaf)
    void     window_switch_fire();                                // Slice 3d: gateway window scheduler (kLayerWindowTimerId) — alternate the active leaf
    void     maybe_emit_gateway_beacon();                         // Slice 3d: per-leaf beacon at window-activation (if the active leaf is due)
    bool     gateway_announce_has_headroom() const;               // rolling airtime < gw_announce_duty_pct % of the duty budget
    void     set_window_anchors(uint8_t active_leaf);             // Slice 3e: refresh each leaf's _next_open_ms (the countdown anchor)
    void     window_grid_now(uint8_t* active_leaf, uint32_t* ms_to_boundary) const;  // Slice 3d GRID: which leaf is active now + ms to its close
    void     store_gateway_schedule(const GatewaySchedule& gs);   // Slice 3e.2: remember a heard gateway's schedule (evict-oldest)
    const GatewaySchedule* find_gw_schedule(uint8_t gw_node_id) const;
    uint32_t gateway_schedule_base_defer_ms(uint8_t gw_node_id, uint32_t* out_jmax) const;  // PURE: base defer + jitter range (no RNG draw)
    uint32_t gateway_schedule_defer_ms(uint8_t gw_node_id);       // Slice 3e.2 SEND path: base + herd-jitter draw (NON-const: draws RNG)
    uint32_t gateway_window_align_beacon(uint32_t nominal_ms);    // gw-window broadcast sync: bias the PERIODIC beacon to a gw-neighbour window-open (NON-const: herd-jitter draw)
    uint8_t  count_direct_neighbors() const;                     // §3e herd sizing: rt entries whose primary candidate is 1-hop
    uint8_t  gateway_spread_nibble() const;                      // §3e: this gateway's 0..15 herd-spread hint (Lua dv:1692)
    uint32_t exchange_airtime_ms() const;                        // §3e: RTS+CTS+gap+DATA+ACK airtime (DATA len = rolling mean)
    // Gateway-doorstep hold (Lua gateway_doorstep_hold@6351): an RTS/ACK timeout to a known gateway —
    // patient window-aware requeue instead of the generic cascade. Returns true if consumed.
    bool     gateway_doorstep_hold();
    // ---- Multi-hop gateway discovery (2026-06-14, type-4 BCN TLV): the originator's gateway SELECTION half ------
    void     ingest_bridged_layer(uint8_t gw_id, uint8_t dest_leaf);   // last-write-wins (one row per gw_id)
    void     prune_aged_bridged_layers(uint64_t now);                  // invalidate rows older than bridged_layers_ttl_ms
    size_t   build_gateway_layer_ext(uint8_t* out, size_t cap);        // our beacon's type-4 TLV (self-advert + re-gossip); 0 = none (s18-inert)
    // ---- Slice 4c.1: cross-layer DM bridge (the keystone) ------------------------------------------------------
    void     bridge_cross_layer(const PostAck& pa, const data_unicast_inner& ui);  // re-inject a transit cross-layer DM onto the far leaf
    int      id_on_leaf_by_hash(uint8_t leaf, uint32_t key_hash32) const;          // resolve key_hash32 -> node_id on a SPECIFIC leaf's id_bind (-1 = unknown); NEVER via _active->
    void     seed_seen_origin_on_leaf(uint8_t leaf, uint8_t origin, uint8_t dst, uint16_t ctr);  // loop-suppress the re-inject on the far leaf
    bool     push_xl_handoff(const XlHandoff& h);                 // buffer a handoff; false = full (refuse loud)
    void     drain_xl_handoffs_for_leaf(uint8_t leaf);           // on activate_layer(leaf): move matching handoffs into the leaf's tx_queue
    // Slice 4a': active_layer_id() (the FULL 8-bit layer_id of the ACTIVE leaf, stamped on every delivered DM/channel
    // record + Push so the app knows which layer a message arrived on) is now PUBLIC (device-console diagnostics block).
    void     start_rts_timeout();
    void     start_ack_timeout();
    void     start_pending_rx_expiry(uint8_t payload_len);
    void     rts_timeout_fire();                                  // :6326
    void     ack_timeout_fire();                                  // :6546
    void     pending_rx_expiry_fire();                            // :6699
    void     tx_rts_retry();                                      // re-pack SAME-ctr_lo RTS
    // R4.5 listen-before-talk. tx_initiating wraps an INITIATING TX (RTS/handle_rts NACK) — LBT pre-check,
    // defer at busy_until + rand(0,lbt_backoff+1) (dv:3680); tx_flood wraps a beacon (LBT + max-defer DROP +
    // duty pre-check, dv:3765); lbt_complete runs the deferred TX (+ the RTS staleness check + start_rts_timeout).
    enum class LbtKind : uint8_t { rts = 0, nack = 1, flood = 2 };
    // R4.5b frame-type tag (echoed by the sim in on_radio_busy; identifies a blocked TX heap-free).
    enum class FrameTag : uint16_t { rts = 0, cts = 1, data = 2, ack = 3, nack = 4, beacon = 5 };
    void     tx_initiating(const uint8_t* bytes, size_t len, int16_t sf, LbtKind kind, uint32_t rts_flight_gen);
    void     rts_duty_defer_fire();                                // cleanup #A redo: re-check duty + hand the deferred RTS (or re-defer / drop-if-stale)
    bool     tx_flood(const uint8_t* bytes, size_t len, int16_t sf);   // false = dropped/skipped (no TX)
    void     lbt_complete(const uint8_t* bytes, size_t len, int16_t sf, LbtKind kind, uint32_t rts_flight_gen);
    bool     schedule_lbt_defer(const uint8_t* bytes, size_t len, int16_t sf, LbtKind kind,   // free-slot stash
                                uint32_t rts_flight_gen, uint32_t delay);   // false = ring full (dropped)
    // NAV (virtual carrier sense, nav_enabled): an overheard unicast RTS/CTS reserves the medium for the rest
    // of that exchange; the node defers its own unsolicited TX (tx_initiating/tx_flood) until it clears. The
    // duration helpers are PURE (native-testable); nav_arm extends _nav_until_ms (max). Conservative SF/size.
    uint32_t nav_duration_rts(uint8_t data_sf, uint8_t payload_len) const;  // overheard RTS -> CTS+DATA+ACK+gaps
    uint32_t nav_duration_cts(uint8_t data_sf, uint8_t payload_len) const;  // overheard CTS -> DATA(exact, or max if payload_len=0)+ACK+gaps
    void     nav_arm(uint32_t duration_ms);                                 // _nav_until_ms = max(_nav_until_ms, now+dur)
    bool     reserve_yield(uint32_t reserve_ms);                            // spec 2026-06-28: push the pending CTS/ACK timeout past an overheard reserve involving our next-hop, NO retry burned; lifetime-bounded (no starvation). Returns true if yielded.
    // R4.5b: the central TX helper (Lua tx_with_retry dv:3599) — stash the retry-eligible frame + set the
    // frame-type tag + duty pre-check + _hal.tx. Every TX except the beacon routes through it.
    bool     tx_with_retry(const uint8_t* bytes, size_t len, int16_t sf, FrameTag tag);   // returns handed (false on a duty defer)
    void     retry_stashed(uint8_t slot);                          // re-issue a stashed frame (kRadioBusyRetryTimerId+slot)
    void     duty_defer_fire(uint8_t slot);                        // re-run tx_with_retry from the stash after a duty defer (kDutyDeferTimerId+slot)
    bool     duty_over_budget(size_t len, int16_t sf, uint32_t* wait_ms);   // check_duty_cycle dv:3573; *wait_ms = defer time when over budget
    static int retry_slot_of(FrameTag tag);                        // FrameTag -> stash slot (0..3) or -1 (not eligible)
    static const char* label_of_frame(FrameTag tag);              // FrameTag -> "RTS"/"CTS"/...
    // ---- cascade-to-alt walk + no-route defer+Q ----------------------------
    uint8_t  pick_next_cascade_hop(const PendingTx& pt);          // two-pass walk :5430; 0 = none (NON-const: refreshes the route order first)
    bool     next_hop_selectable(const RtCandidate& c, const PendingTx& pt,
                                 bool allow_uphill) const;        // minimal filter :3990
    void     cascade_to_alt(const char* trigger);                 // on giveup: switch hop or requeue :6456
    void     try_cascade_requeue(const PendingTx& pt, const char* giveup_event);  // exhaustion -> requeue/giveup :6190
    static SendFailReason giveup_fail_reason(const char* giveup_event);   // Slice 6b: "rts_*"->no_cts, "data_ack_*"->no_ack, else none
public:
    // ④ load-adaptive cascade budget (Lua cascade_load_skip dv:6275): the effective requeue budget at a given TX-queue
    // depth = cascade_requeue_max − max(0, depth − threshold), clamped ≥0. Pure (depth + constants); static for tests.
    static int cascade_effective_max(uint8_t queue_depth);
private:
    uint32_t requeue_backoff_ms(uint8_t requeue_count) const;     // pure base*2^(n-1) capped :6209
    uint8_t  effective_rts_max_retries(uint8_t requeue_count) const;  // max(0, max-requeue_count) :3119
    void     defer_send(const TxItem& item);                      // no route yet -> hold (originator) :5545
    void     try_drain_deferred();                                // TTL-first, route-exists drain :6765
    bool     alt_tried(const PendingTx& pt, uint8_t hop) const;
    void     mark_tried(PendingTx& pt, uint8_t hop);
    uint16_t next_ctr(uint8_t dst);                               // per-(self,dst) counter (NOT rand)
    uint8_t  select_data_sf(uint8_t rts_sf_index, int16_t rx_snr_q4) const;  // adaptive DATA SF, Lua :3043+:3027
    uint32_t airtime_routing_ms(uint16_t len) const;             // floor-exact, for timeout sizing
    // Anti-spam v2 (MF3/MF8): the channel-capacity C = max(1, D/T_ch) with T_ch = RTS-M + DATA-M airtime. THE single
    // source of C — BOTH channel_cap_origin() (the enforced cap) and limits_snapshot() (the ch_ceiling shown to the
    // user) call it, so the displayed ceiling can never drift from the enforced math. Returns 0 when duty is disabled.
    uint32_t channel_capacity_C() const;
    // retry_jitter_ms() is declared in the public section (R3.x golden test).

    Hal&     _hal;
    uint8_t  _node_id;            // reassignable via _hal.set_protocol_id (join/lease)
    uint32_t _key_hash32;         // stable long identity
    uint8_t  _x_secret[32] = {};  // DP1: X25519 ECDH secret (Phase-1 E2E DM crypto)
    uint8_t  _ed_pub[32]   = {};  // DP1: our Ed25519 pubkey (advertised so peers can ECDH to us)
    bool     _crypto_ready = false;
    NodeConfig _cfg;             // borrowed copy from on_init
    Inbox    _inbox;             // persistent inbox (disabled until a backend installs stores; see inbox())
    int16_t  _routing_snr_floor_q4 = 0;   // SF_DEMOD_THRESHOLD[routing_sf] + sf_margin_q4
    bool     _rt_full_emitted = false;
    // R2 state
    uint8_t  _beacon_offset = 0;             // sliding stable-page rotation cursor
    bool     _discovery_mode = false;        // fast cadence + full pages until exit
    uint64_t _discovery_started_ms = 0;
    uint64_t _discovery_until_ms = 0;
    uint16_t _discovery_bcn_rx_count = 0;
    bool     _pending_rediscover = false;     // reprovision verb -> restart discovery at the next join_adopt (id stable)
    bool     _triggered_beacon_pending = false;  // coalesce: gates BEFORE the rand draw
    uint64_t _last_beacon_tx_ms = 0;
    uint64_t _duty_cycle_budget_ms = 0;          // R4.0: floor(duty_cycle*window), derived in on_init; 0 = disabled
    uint16_t _dm_payload_mean = 0;               // §3e: EWMA (alpha 5/16) of DATA payloads we pass; 0 = no sample (use assumption)
    uint64_t _window_epoch_ms = 0;               // Slice 3d GRID: the absolute anchor (boot instant = leaf-0's first window open).
                                                 // Switch times live on the grid epoch + k·period (+window0); a busy slip never ratchets it.
    // R4.3 adaptive-throttle witnesses (channel-busy detector). Pure timestamps, no rand.
    uint64_t _last_rx_routing_sf_ms = 0;         // any successful decode OR preamble-detect (dv:9164/12231); 0 = never
    uint64_t _last_rx_bcn_ms        = 0;         // last beacon ingest (the max-idle B+C filter; dv:9559)
    // R4.5 LBT: derived delays (on_init) + a small RING of deferred-TX slots. The Lua uses independent
    // per-defer closures (dv:3704/3808), so two concurrent busy-channel defers BOTH fire — a single stash
    // would drop the first + desync the rand stream. Each slot has its own timer id (kLbtDeferTimerId+slot).
    // buf holds a full beacon (beacon_max_bytes=151) — a smaller buf would TRUNCATE a deferred page (review #04).
    uint32_t _lbt_backoff_ms        = 0;
    uint32_t _flood_lbt_max_defer_ms = 0;
    uint64_t _nav_until_ms          = 0;         // NAV: medium reserved (by an overheard unicast RTS/CTS) until this ms; 0 = clear
    static constexpr uint8_t kLbtSlots = 4;
    struct DeferredLbt { bool pending = false; uint8_t kind = 0; uint8_t len = 0; int16_t sf = 0;
                         uint32_t rts_flight_gen = 0;   // RTS staleness key (flight_gen, not the old 4-bit ctr_lo proxy)
                         uint8_t buf[protocol::beacon_max_bytes] = {}; };
    DeferredLbt _deferred_lbt[kLbtSlots];
    // Cleanup #A redo: an over-budget RTS is duty-deferred in a DEDICATED slot (NOT the shared LBT ring — that reuse
    // was net-worse, review wgvbtirmu). One slot: there is only ever one pending_tx/flight. flight_gen staleness makes
    // the long (~1h) duty wait safe. kRtsDutyDeferTimerId fires rts_duty_defer_fire (re-check duty / hand / re-defer).
    uint32_t _flight_gen = 0;     // monotonic; bumped per new pending_tx (issue_send)
    struct RtsDutyDefer { bool pending = false; uint16_t len = 0; int16_t sf = 0; uint32_t flight_gen = 0;
                          uint8_t buf[16] = {}; };   // RTS pack is <=9 B (RTS_LEN 8 + M-broadcast 2)
    RtsDutyDefer _rts_duty_defer;
    // R4.5b on_radio_busy retry: a per-frame-type tag (echoed by the sim) lets on_radio_busy identify a blocked
    // TX (heap-free, no string label). The retry-eligible frames (CTS/DATA/ACK/NACK; RTS/beacon are NOT) are
    // STASHED so a busy-channel block re-issues them up to TX_DEFER_MAX_RETRIES. tx_stash keyed by the retry slot.
    static constexpr uint8_t kRetrySlots = 4;   // cts, data, ack, nack
    struct TxStashSlot { bool valid = false; uint16_t len = 0; int16_t sf = 0; uint8_t retries_left = 0;
                         uint8_t ctr_lo = 0;   // DATA slot: the pending_tx flight this DATA belongs to (re-arm guard, dv:10271)
                         // reissue_pending: a busy/duty re-issue timer is ARMED for this slot (vs. a stale clean-sent
                         // buffer that is `valid` but already on the air). layer_swap_blocked() gates on THIS, not
                         // `valid` — else a gateway's first cleanly-sent ACK leaves `valid` set forever + the layer
                         // swap never fires (the bridged DM on the other leaf never transmits). See node.cpp swap guard.
                         bool reissue_pending = false;
                         uint8_t buf[protocol::lora_max_frame_bytes] = {}; };
    TxStashSlot _tx_stash[kRetrySlots];
    // R3 data-plane state (single flight per node) — the pipeline arrays MOVED into LayerRuntime (Slice 2a).
    // kTxQueueCap stays a Node-level static constexpr (compile-time array dim, identical for every layer;
    // visible by unqualified name inside the nested LayerRuntime + in Node member fns — no per-layer state).
    static constexpr uint8_t kTxQueueCap = 8;
    // F route-discovery dedup state (Lua route_request_seen / route_request_last). Members in LayerRuntime.
    struct RReqSeen { uint8_t origin; uint8_t dst; uint64_t t_ms; };   // relay flood-dedup
    struct RReqLast { uint8_t dst; uint8_t ttl; uint64_t t_ms; };      // per-dst origination rate-limit
    // Hash-locate id_bind table (Lua dv:4677): key_hash32 -> node_id, beacon-populated. Bounded array
    // (array sized at the protocol max; _cfg.cap_id_bind gates additions). One timestamp: id_bind_set
    // always carries the key, so last_seen == last_key_seen (the plain-refresh split lands with C.2). Member in LayerRuntime.
    struct IdBind { uint32_t key_hash32; uint64_t last_seen_ms; uint8_t node_id; uint8_t source; uint8_t confidence; };
    // E2E peer-pubkey cache (Phase 1 §6): key_hash32 -> ed_pub. Immutable + hash-verifiable (ed_pub[:4]==key_hash32),
    // so a TYPE-5 owner answer is cached AUTHORITATIVE even relayed/cached-on-pass (can't decay). Member in LayerRuntime.
    struct PeerKey { uint32_t key_hash32; uint64_t last_seen_ms; uint8_t ed_pub[32]; uint8_t confidence; };
    // H hash-locate flood dedup (Lua hash_query_seen): per-(origin,key_hash32), hash_query_seen_ttl_ms window. Member in LayerRuntime.
    struct HashQuerySeen { uint8_t origin; uint32_t key_hash32; uint64_t t_ms; bool hard; bool want_pubkey; };   // §2: WANT_PUBKEY is its own variant
    // Peer-liveness + freshness plane (routing-liveness port, Lua dv:3986-4545): per-next-hop RTS/ACK-timeout
    // accounting -> suspect/silent/dead tiers (each with an expiry), + dest_seen for next-hop freshness. Bounded
    // LRU table per LayerRuntime (the direct-neighbour set). node_id 0 = empty slot.
    // §P4 gossip: suspect_advertise_until_ms / dead_advertise_until_ms hold the GOSSIP window (what to put in our BCN
    // suspect-TLV), set ONLY by LOCAL rts_timeout evidence (mark_peer_suspect remote_src==0). REMOTE-learned tiers write
    // the *_until_ms routing fields but NOT these -> a node never re-gossips a suspicion it heard (anti-storm, dv:1388).
    struct PeerLiveness { uint8_t node_id; uint16_t rts_timeouts; uint64_t first_timeout_ms;
                          uint64_t suspect_until_ms; uint64_t silent_until_ms; uint64_t dead_until_ms; uint64_t dest_seen_ms;
                          uint64_t suspect_advertise_until_ms; uint64_t dead_advertise_until_ms;
                          uint64_t e2e_ack_spoof_until_ms = 0; };   // anti-spoof: while now < this, the peer's RTS_FLAG_E2E_ACK is IGNORED (backstop re-applies)
    // send-by-hash DMs parked awaiting a hash-bind resolution (D); drained by on_hash_bind_response, aged on the timer.
    // is_redirect=true => an L2c misdelivered DM held for FORWARD (not re-send): `body`=the full inner (incl.
    // DST_HASH), and origin/ctr/ctr_lo are preserved so the resolution forwards it identity-intact. The redirect
    // leg is re-budgeted as a fresh route (originator-style), so no hop fields are carried. resolved_id==our id
    // at drain = a CONFIRMED collision (the heal trigger, design §7.1).
    struct ParkedSend { uint32_t key_hash32; uint64_t parked_at_ms; uint8_t flags; uint8_t body_len;
                        bool is_redirect = false; bool is_resolve = false; bool cross_layer = false; uint8_t origin = 0; uint16_t ctr = 0; uint8_t ctr_lo = 0;
                        uint8_t type = 0;   // S1/M7a: a redirect's DataType (E2E_ACK/H_ANSWER); preserved across park+heal so the forwarded frame keeps its type (only meaningful when is_redirect)
                        uint8_t nonce_seed[8] = {};   // §1c: a CRYPTED redirect's originator seed (preserved across the park+heal); zero for a plain send (re-sealed on drain)
                        uint8_t body[protocol::max_payload_bytes_hard_cap]; };   // is_resolve: notify-only diag (a `resolve`), no body. cross_layer (Slice 4d): a send_layer awaiting (node_id,target_layer)
    ParkedSend _parked_sends[protocol::cap_parked_sends] = {};
    uint8_t    _parked_sends_n = 0;
    // L2c redirect-suppression ring: a misdelivered DM we've already redirected for this hash recently,
    // so a still-poisoned binding (collision unhealed) can't re-trigger an endless redirect→deliver→redirect.
    struct L2cRedirect { uint32_t key_hash32; uint64_t t_ms; };
    L2cRedirect _l2c_redirect[protocol::cap_l2c_redirect] = {};
    uint8_t     _l2c_redirect_n = 0;
    // node_id auto-assignment (DAD + heal) — node_join.cpp; design 2026-06-05-node-id-auto-assignment-design.md.
    bool     _joined = false;                                    // adopted a node_id via DAD (vs cfg/NV-provisioned)
    bool     _join_listen_pending = false;                       // a join was requested; listening before the first claim (L1)
    uint8_t  _claim_epoch = 0;                                   // VESTIGIAL (key-only tiebreak): reserved on wire/NV, not consulted
    struct JoinClaim { bool active; uint8_t proposed; uint32_t key_hash32; uint8_t claim_epoch; uint8_t nonce; uint64_t started_ms; };
    JoinClaim _join_claim{};                                     // the single in-flight claim (active=false when none)
    struct DeniedId { uint8_t id; uint64_t denied_at_ms; };      // a slot that lost a claim/heal (§13: 1-day TTL)
    DeniedId _join_denied[protocol::cap_join_denied] = {};
    uint8_t  _join_denied_n = 0;
    struct MediatedRecent { uint8_t node_id; uint32_t loser_hash; uint64_t t_ms; };   // L2a: suppress per-(id,loser) re-DENY
    MediatedRecent _mediated_recent[protocol::cap_mediated_recent] = {};
    uint8_t        _mediated_recent_n = 0;
    // Q REQ_SYNC plane state (node_query.cpp). _last_req_sync_tx_ms rate-limits the originator (dv:8035);
    // _q_responded is the responder dedup ring (key opcode|src|dest, ttl q_respond_ttl_ms) — Lua refuses on
    // cap-full, we evict-oldest (matches the F-dedup idiom; equivalent below cap, robust for a long-running
    // device). _sync_pending is the bounded jitter-response ring (Lua: an unbounded table of after()-closures;
    // one slot per requester, fired by kSyncResponseTimerId+slot). Both arrays MOVED into LayerRuntime (Slice 2b):
    // keyed by a REMOTE leaf-local id (q.src / requester), so a gateway's two leaves must NOT share them
    // (Principle 5 — else node-5@leafA aliases node-5@leafB and the gateway drops one leaf's sync reply).
    uint64_t _last_req_sync_tx_ms = 0;   // self-state (our own last REQ_SYNC tx) — stays Node-global, not per-layer
    uint64_t _last_config_pull_tx_ms = 0;   // R6.2: rate-limit our CONFIG_PULL tx
    uint16_t _max_seen_epoch = 0;           // R6.3 §4.1: highest config_epoch seen for OUR lineage (a write = max_seen+1)
    uint64_t _last_join_refused_ms = 0;     // R6.3 §7c: rate-limit the join_refused{wire_version} push
    struct QResponded { uint8_t opcode; uint8_t src; uint8_t dest; uint64_t t_ms; };
    struct SyncPending { bool active; bool suppressed; uint8_t requester; bool requester_mobile;
                         uint64_t requested_at; uint64_t fire_at; };
    // Channel-message gossip plane state + dedup maps + the originator ring — MOVED into LayerRuntime (Slice 2a;
    // struct defs ChannelEntry/FloodState/etc. are above the channel method decls; OrigEvent/OrigRing below).
    // R4.4 originator anti-spam: per-sender sliding-window ledger of overheard RTS/CTS. kind: 0=rts, 1=cts.
    // FIXED RING (not a std::vector) — the old map-of-vectors rebuilt a vector on every overheard frame
    // (alloc/free per frame), which fragments the nRF52 heap; this keeps the events in a fixed in-struct
    // array (no per-frame heap), evicting the oldest on overflow. Insertion-ordered so the dedup-FIRST
    // refresh still matches the Lua ipairs scan. The std::map (sender -> ring) stays — its node alloc is
    // once per NEW sender (bounded by neighbours), not per frame (the accepted determinism relaxation).
    struct OrigEvent { uint64_t t; uint8_t kind; uint8_t ctr_lo; uint32_t air; };
    struct OrigRing  { OrigEvent ev[protocol::cap_originator_events]; uint8_t count = 0; };
    // _per_sender_originator MOVED into LayerRuntime (Slice 2a).

    // ---- LayerRuntime (2026-06-12-gateway-dual-layer-design.md §2) -----------------------------------------
    // Per-layer (per-leaf) runtime state. A normal node has n_layers=1 and only _layers[0] is used; a gateway
    // (later slices) has 2 EQUAL layers and swaps _active at each window switch. Slice 2a is a PURE NO-OP hoist:
    // these members moved here VERBATIM (initializers preserved), every reader redirected through _active->.
    // The array dimension is MR_N_LAYERS (protocol_constants.h): 1 on a leaf build (the array is one element —
    // identical RAM to the pre-dual-layer firmware), 2 only on [env:gateway]. on_init REFUSES n_layers==2 when
    // MR_N_LAYERS<2, so _layers[1] is never reached on a 1-element build (audited: 405 reads all go via _active=&_layers[0]).
    // Nested struct so the in-class helper struct defs (RtEntry, TxItem, PendingTx, ..., IdBind, ChannelEntry,
    // FloodState, OrigRing, ...) stay visible by unqualified name. Node is never copied, so the raw _active
    // pointer + default member initializer is safe.
    struct LayerRuntime {
        // Routing table (DV).
        RtEntry  _rt[protocol::cap_routes];
        uint8_t  _rt_count = 0;       // distinct dests, kept sorted ascending by dest
        // R3 data-plane state (single flight per node).
        TxItem                   _tx_queue[kTxQueueCap];
        uint8_t                  _tx_queue_n = 0;          // FIFO depth
        std::optional<PendingTx> _pending_tx;
        std::optional<PendingRx> _pending_rx;
        PostAck                  _post_ack;
        // No-route defer queue (insertion-order array; drained TTL-first on a beacon
        // route-change or the 1s periodic timer). _drain_armed gates the periodic timer.
        DeferredSend             _deferred[protocol::cap_deferred_sends];
        uint8_t                  _deferred_n = 0;
        bool                     _drain_armed = false;
        // F route-discovery dedup state (Lua route_request_seen / route_request_last).
        RReqSeen _rreq_seen[protocol::cap_route_request_seen] = {};
        uint8_t  _rreq_seen_n = 0;
        RReqLast _rreq_last[protocol::cap_route_request_last] = {};
        uint8_t  _rreq_last_n = 0;
        // Hash-locate id_bind table (Lua dv:4677): key_hash32 -> node_id, beacon-populated.
        IdBind   _id_bind[protocol::cap_id_bind] = {};
        uint16_t _id_bind_n = 0;
        // E2E peer-pubkey cache (Phase 1 §6): key_hash32 -> ed_pub (authoritative, hash-verified).
        PeerKey  _peer_keys[protocol::cap_peer_keys] = {};
        uint16_t _peer_keys_n = 0;
        // H hash-locate flood dedup (Lua hash_query_seen): per-(origin,key_hash32), hash_query_seen_ttl_ms window.
        HashQuerySeen _hash_query_seen[protocol::cap_hash_query_seen] = {};
        uint8_t       _hash_query_seen_n = 0;
        // Peer-liveness + freshness plane (routing-liveness port): per-next-hop timeout tiers. Bounded LRU.
        PeerLiveness  _peer_liveness[protocol::cap_peer_liveness] = {};
        uint8_t       _peer_liveness_n = 0;
        // dest_seen freshness map (Lua dest_seen_ms@1289): node_id -> last-seen ms, FULL 0..254 range, NO eviction
        // — decoupled from the bounded _peer_liveness table so seen-bitmap gossip can keep ANY peer fresh (the
        // create=false piggyback starved gossip-only peers). is_next_hop_fresh reads this. 0 = never seen.
        uint64_t      _dest_seen_ms[256] = {};
        // Bidirectionality plane (asymmetric-link routing, 2026-06-29). Index = node_id, value = a LinkBidi.
        // Zero-init => every link defaults to 'unknown' (selectable, unpenalized). FULL 0..254 range, NO eviction
        // (like _dest_seen_ms) so a gossip-only or quiet peer keeps its state. _link_bidi_confirmed_ms is the
        // DEDICATED decay source — last real-CTS / complete-heard-set confirmation time (do NOT overload _dest_seen_ms).
        uint8_t       _link_bidi[256] = {};
        uint64_t      _link_bidi_confirmed_ms[256] = {};
        // ① mobile-peer set (Lua mobile_peers@1325): 1 bit per node_id, SET-only (is_mobile is a static per-node config,
        // never flips at runtime — Lua dv:9603-9604 sets, never clears). Eviction-free (unlike _peer_liveness) so a
        // gossip-only mobile is still avoided. 256 bits = 32 B/layer. Read by is_mobile_peer.
        uint8_t       _mobile_peer[32] = {};
        // Slow-reprobe throttle (asymmetric-link slice 6): per-next-hop last single-probe time for a
        // _link_bidi==one_way sole route. FULL 0..255 range, eviction-free (like _dest_seen_ms) so an
        // isolated next-hop is throttled even if its PeerLiveness slot was LRU-evicted. 0 = never reprobed
        // (clock-at-0 -> the FIRST giveup probes immediately, then once per link_reprobe_ttl_ms).
        uint64_t      _link_reprobe_last_ms[256] = {};
        // Channel-message gossip plane state (node_channel.cpp).
        ChannelEntry _channel_buffer[protocol::cap_channel_buffer];
        uint16_t     _channel_buffer_n = 0;
        std::map<uint8_t, ChannelOriginLedger> _per_origin_channel;   // origin -> windowed distinct-id ledger
        ChannelPullPending _channel_pull_pending[protocol::cap_channel_pull_pending] = {};
        ChannelPullRecent  _channel_pull_recent[protocol::cap_channel_pull_recent] = {};
        uint8_t            _channel_pull_recent_n = 0;
        FloodState         _flood[protocol::cap_flood_pending] = {};   // channel-flood in-progress table (slot i -> timer kFloodRebcastTimerId+i)
        ChannelReofferPending _channel_reoffer_pending[protocol::cap_channel_reoffer_pending] = {};  // Part 2: per-origin re-offer table (slot i -> timer kChannelReofferTimerId+i)
        // dedup maps.
        std::map<uint8_t, uint16_t>  _peer_send_counter;   // next_ctr per dst
        uint16_t     _peer_ctr_floor = 0;                  // D7: per-peer next_ctr floor (persisted high-water; resumes DM ctrs above the pre-reboot value)
        std::map<uint32_t, LastAcked> _last_acked_from;    // key (src<<24|dst<<16|ctr_lo<<8|len)
        std::map<uint64_t, uint64_t>  _seen_origins;       // §1b TYPE-NAMESPACED flight key -> expiry_ms. PLAINTEXT =
                                                           // (origin<<24|dst<<16|ctr) in [0,2^32); CRYPTED = the full
                                                           // 8-B nonce-seed | (1<<63) in [2^63,2^64) — disjoint, can't alias.
        std::map<uint64_t, uint8_t>   _seen_origin_from;   // same key -> the prev-hop (LOOP_DUP discriminator)
        std::map<uint8_t, uint64_t>   _blind_until;        // next_hop -> absolute_ms it's deaf-on-routing (F1)
        // R4.2 persistent neighbor budget tier (routing-grade demotion beyond the short blind window).
        // mutable: get_neighbor_tier lazy-prunes the TTL-expired entry on read, like the Lua (dv:3863-3868).
        mutable std::map<uint8_t, uint8_t>  _neighbor_budget_tier;       // next_hop -> tier (1..3); absent/0 = HEALTHY
        mutable std::map<uint8_t, uint64_t> _neighbor_budget_tier_set_at; // next_hop -> absolute_ms the mark was set
        // R4.4 originator anti-spam: per-sender sliding-window ledger of overheard RTS/CTS (fixed ring).
        std::map<uint8_t, OrigRing> _per_sender_originator;  // sender_id -> recent events (fixed ring)
        // Q REQ_SYNC plane dedup (Slice 2b — moved from Node scope; keyed by a REMOTE leaf-local q.src/requester,
        // so per-layer: a gateway's two leaves must not alias the same 8-bit id across distinct physical nodes).
        QResponded  _q_responded[protocol::cap_q_responded_to] = {};            // responder dedup ring (opcode|src|dest)
        uint8_t     _q_responded_n = 0;
        SyncPending _sync_pending[protocol::cap_sync_response_pending] = {};    // jittered full-table reply ring (per requester)
        // Slice 3d per-leaf beacon: a gateway beacons each leaf on its OWN cadence at window-activation (the shared
        // kBeaconTimerId is disabled for gateways — its single deadline halves the per-leaf cadence). 0 = never beaconed.
        uint64_t _last_beacon_ms = 0;
        // Slice 3e: absolute ms this leaf's window NEXT opens — the anchor for the receiver-anchored countdown
        // (schedule_record.offset). Set by the scheduler on every switch + at boot. countdown = (next_open-now) % period.
        uint64_t _next_open_ms = 0;
    };
#ifdef MESHROUTE_NATIVE
    // White-box test seam (native test build only — #ifdef'd out of every device build, zero firmware surface):
    // test/test_dual_layer.cpp points _active at each leaf + reads the per-LayerRuntime dedup maps to ASSERT the
    // Slice-2b non-aliasing property (§8). The gateway's real leaf-swap (activate_layer) lands in Slice 3.
    friend struct DualLayerTestAccess;
#endif
    LayerRuntime  _layers[MR_N_LAYERS];
    LayerRuntime* _active = &_layers[0];
    uint8_t       _n_layers = 1;
    // Slice 3e.2: learned schedules of nearby GATEWAYS (Node-global — a node's view of the gateways it can reach,
    // independent of its own layers). Keyed by the gateway's node_id; evict-oldest on overflow.
    GatewaySchedule _gw_schedules[protocol::cap_gateway_neighbor_schedules];
    BridgedLayer    _bridged_layers[protocol::cap_bridged_layers];   // multi-hop gw_id->dest_leaf (type-4 TLV; ~8×11 B)
    // Slice 4c.1: cross-layer re-inject HANDOFFS (node-global — they span leaves; survive a window swap, drained on the
    // TARGET leaf's activate). A SMALL bounded ring (refuse-when-full LOUD, never drop-oldest a transit DM silently).
    XlHandoff _xl_handoffs[protocol::cap_gateway_handoffs];

    uint64_t _ack_warn_until = 0;   // DM Inc 3: park new DM originations until this ms (set by a warn'd ACK)
    uint64_t _last_channel_origin_ms = 0;   // Slice 2: self side of channel_min_interval_ms (own channel posts)
    uint64_t _last_dm_origin_ms = 0;   // Slice 3: own-DM burst floor (dm_min_interval_ms); relays/floods/e2e-ack/rcmd exempt
    // NACK BUSY_RX wait-same-hop: the captured ctr_lo the kNackWaitTimerId re-RTSes for.
    uint8_t                      _nack_wait_ctr_lo = 0;
    bool                         _nack_wait_pending = false;
    // async push ring (the app channel; drained via next_push, drop-oldest on overflow)
    Push     _push_ring[protocol::cap_push_ring];
    uint8_t  _push_head = 0, _push_count = 0;
    RemoteInbound _remote_inbound{};   // `rcmd`: single inbound slot (one in flight; a 2nd while pending drops). Drained by fw_main.
};

}  // namespace meshroute
