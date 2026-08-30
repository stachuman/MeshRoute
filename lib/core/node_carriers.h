// MeshRoute — lib/core/node_carriers.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The Node's shared VALUE-CARRIER types — LayerConfig/NodeConfig/RtCandidate/RtEntry/TxItem/PendingTx/DeferredSend/
// PendingRx/PostAck/GatewaySchedule/BridgedLayer/XlHandoff + the enums (LinkBidi/Plane/GwValErr/GwParseErr).
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
// §hybrid-rts S2: the flight-identity POD only (`RtsFlightIdentity` + its ONE comparator). dm_crypto.h itself
// depends on nothing but <cstdint>/<cstddef>, so this keeps the header's "no Node/frame_codec deps" property.
#include "dm_crypto.h"

namespace MESHROUTE_NS {

// Per-layer (per-leaf) config — the dual-layer gateway model (2026-06-12-gateway-dual-layer-design.md §3.1).
// A normal node has n_layers=1 (uses layers[0]); a GATEWAY has 2 EQUAL layers (no home/guest, §0.1). One
// identity (key_hash32) spans both; node_id is per-leaf (independent DAD, §0.2). `layer_id` is the FULL 8-bit
// id (1..255); `leaf_id = layer_id & 0x0F` is the derived coarse wire filter (§0.8). window_ms/offset 0 =
// DERIVE the SF/BW-weighted anti-phase split at the scheduler (§4, Slice 3). REQUIRED on a gateway: layer_id /
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
                                          // ⚠ Deliberately NOT an input to the window-split weighting (unlike SF/BW):
                                          // CR is auto-detected from the explicit PHY header, so it is a property of a
                                          // transmission, not of the layer. Rule + rationale at the derive in node.cpp.
    uint32_t beacon_period_ms  = 900000;
    uint32_t window_period_ms  = 15000;   // the full layer0->layer1 cycle (§3.2 default; cfg-overridable)
    uint32_t window_ms         = 0;       // this layer's presence in the cycle; 0 = DERIVE SF/BW-weighted, NOT CR-weighted (§4)
    uint32_t window_offset_ms  = 0;       // phase; 0 = DERIVE anti-phase from the other layer (§4)
};

// §3.2 dual-layer validation result. The SHARED predicate validate_gateway_layers() returns this; on_init maps
// non-ok -> refuse (bool false), the `gateway` console command maps it to a specific operator error message.
enum class GwValErr : uint8_t {
    ok = 0, bad_leaf, bad_ctrl_sf, no_data_sf, leaf_nibble_clash,
    period_mismatch, period_zero, window_degenerate, window_zero, window_exceeds_period, window_overlap, window_too_long,
    bad_bw, bad_cr,  // v17 per-layer PHY: bw_hz not a legal SX1262 bandwidth / cr not in 5..8 (0 = inherit is always ok)
    freq_inherit_no_global   // §layer-freq: one layer sets freq_mhz, the other inherits, and radio_freq_mhz is 0 —
                             // the inherit-leaf could never be retuned back, so it would silently run the other
                             // leaf's carrier. Refuse. (Both inherit + no global = the legacy single-carrier world,
                             // still fine: the radio never moves. Both override = no global needed.)
};
// The ONE dual-layer gate, shared by on_init AND parse_gateway_cmd (so the console command can never accept a
// config on_init would refuse — anti-drift). Validates both layers' required fields + the leaf-nibble rule + the
// shared window period, DERIVES the SF/BW-weighted anti-phase window split for any window_ms/offset left 0 (CR is
// DELIBERATELY not part of that weighting — see the rule at the derive in node.cpp), and
// validates the concrete schedule. MUTATES L0/L1 window fields in place. Pure (no Serial/NV).
GwValErr validate_gateway_layers(LayerConfig& l0, LayerConfig& l1, uint32_t radio_bw_hz, uint8_t radio_cr,
                                 double radio_freq_mhz);

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
    uint8_t  team_dad_pin_id     = 0;           // §W2c WHITE-BOX TEST/SIM HOOK (0 = OFF = normal random team-DAD pick): pin the FIRST team-DAD candidate id so a hidden-terminal COLLISION is deterministic in-sim (two members pin the same id). A RE-PICK (mediated DENY / direct collision) IGNORES the pin -> falls to the random picker, so convergence is unaffected. NOT a protocol knob — only s30 sets it; every real deployment / other scenario leaves it 0 (team-DAD stays uniformly random) -> byte-identical.
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
    uint32_t team_beacon_period_ms = 300000;     // §team-multihop (spec 2026-07-15 Change A): a TEAM member's STEADY-state beacon period (5 min default) — 3× more responsive than static's 15 min (a roaming team) yet safely under the 20-min freshness (next_hop_live_ttl_ms) + 15-min silent (peer_silent_ttl_ms) ceilings. Team members drop to THIS after the 5 s discovery burst instead of beacon_period_ms; a static node never uses it. Per-scenario configurable (sim uses a faster value so DV converges in-run).
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
    // §team-parity T0: the TEAM plane's twin of dv_hop_cap (default 8, spec §3/T0 + R4). Read ONLY through
    // Node::hop_cap_for(team_plane) — never index a cap by hand. NOT MR_FEAT_TEAM-gated, deliberately: the
    // accessor must compile identically on the three gateway_* envs (MR_FEAT_TEAM 0), so a later slice cannot
    // route a live path through a team-gated stub the corpus is blind to (the 2026-07-27ze near-miss).
    // ★ LIVE SINCE T1 — the team discovery plane reads it: the cascade-exhaustion + deferred-drain RREQ TTLs
    // (node_cascade.cpp:145/:317) and the RREQ/RREP hop-cap backstops (node_route_discovery.cpp:273).
    // ✖ STILL NOT the team DV combined-hops cap (node_beacon.cpp:884) — that site deliberately still reads
    // dv_hop_cap, so the team radius remains RREQ-8 / DV-16. T3 attempted the flip, measured it and BACKED IT OUT:
    // it is inert on the 9 scenarios T0 predicted (0 of 9 move) and its only effect is to disarm s35a/s38, which no
    // value of this knob can restore. The full measurement and the structural reason live at node_beacon.cpp:859-891
    // — read it there before re-attempting the flip.
    // ★ CONSOLE SURFACE — DONE in T3 (was MISSING), mirroring dv_hop_cap's EXACTLY and no further: `cfg set
    // team_hop_cap` (firmware_config.cpp, same valid_hop_cap 1..16 domain, refuses loud, `persist = false` =
    // LIVE-only so reboot reverts to protocol::team_hop_cap) + the three readouts beside dv_hop_cap's
    // (firmware_commands.cpp dump_cfg, console_json.cpp write_cfg, console_binary.cpp enc_cfg via the APPENDED
    // TAG_CFG_TEAM_HOP_CAP=0x1C) + the simulator's NodeRuntimeWrapper.cpp dispatch row.
    // ★ STILL MISSING, deliberately: no NV blob and no J-frame field (C4 — no wire change, nothing to reflash for).
    // dv_hop_cap's own console knob is likewise `persist = false`, so there is no NV blob to extend either. WHY NOT:
    // R4 fixes the team ceiling at 8 by design (3-10 members, stragglers to 8), unlike dv_hop_cap which is a
    // per-network provisioning choice carried on the J frame. A PERSISTED team knob would invite per-node divergence
    // on a plane that has no provisioning authority to reconcile it — every member self-DADs. The live-only knob is
    // the right shape: a bench/scenario can clip the radius for a run, and a reboot restores the design value.
    // Placed here (immediately after dv_hop_cap, ahead of the 8-byte-aligned `double radio_freq_mhz` below) so it
    // lands in the SAME existing alignment padding the radio_freq_mhz note describes: measured native offsets are
    // dv_hop_cap@93 / radio_freq_mhz@96, i.e. bytes 94-95 are pure pad. Cost measured: sizeof(NodeConfig) 264 -> 264.
    uint8_t  team_hop_cap = protocol::team_hop_cap;
    // ★★ §chan-crypt CL2a (T-K2 §2.5, spec 2026-07-30 §2.2): SEAL a `-t` team channel post by default whenever this
    // node HOLDS the team content key. DEFAULT ON — the privacy-safe direction, and the reason `-e` is mostly
    // unnecessary in practice: `-e` exists to be EXPLICIT and to fail loud when sealing is impossible.
    // The effective decision is `(-e) || (team_channel_crypt && team_channel_priv() != nullptr)` — ONE line in
    // Node::on_command's send_channel arm, so both permanent refusals (`-e` without `-t`, and `-t -g -e`) cover the
    // IMPLICIT seal automatically. A node WITHOUT the key posts plaintext exactly as before (unchanged), and
    // keyholders still read it — a plaintext flavor is always openable.
    // ⚠ INERT ACROSS THE WHOLE SIM CORPUS BY CONSTRUCTION: no scenario can hold a team content key (every establish
    // path — mint / adopt / adopt_priv / NV load / the sealed DATA_TYPE_TEAM_KEY_GRANT — is reachable only from `src/`, which
    // the simulator does not build). That is WHY flipping this default to ON moved 0 of 36 streams.
    // ★ OPT-OUT IS THE CONFIG TOGGLE ONLY (`cfg set team_channel_crypt 0`), open decision O2: a per-send
    // "air this one in clear" flag is a footgun on a privacy feature. PERSISTED (device_nv v24) — an operator
    // setting that silently reverted on reboot is the footgun the toggle exists to avoid.
    // Placed here (immediately after team_hop_cap, ahead of the 8-byte-aligned `double radio_freq_mhz`) so it lands in
    // the ONE remaining pad byte of the same hole team_hop_cap's note describes: dv_hop_cap@93, team_hop_cap@94,
    // radio_freq_mhz@96 ⇒ byte 95 was pure pad. Cost MEASURED: sizeof(NodeConfig) 256 -> 256. SEVENTH application of
    // the radio_freq_mhz / team_hop_cap / HashQuerySeen.team_scoped / T5-PeerLiveness / T-K1-_team_ch_key_present /
    // AB4-_peer_loc_n padding-placement rule.
    bool     team_channel_crypt = true;
    // §layer-freq (2026-07-27): the node's GLOBAL RF carrier, MHz — the freq twin of radio_bw_hz/radio_cr.
    // Sources: the firmware's LORA_FREQ / persisted nv.freq_mhz (fw_main) and the sim node's freq_khz
    // (injected as _sim_freq_khz). It exists for ONE reason: active_freq_mhz() must be able to RESET the HAL
    // back to the global when an INHERIT layer (freq_mhz==0) is entered after an OVERRIDE layer — see
    // activate_layer, where freq used to be the only PHY knob that skipped the reset.
    // ⚠ SCOPE, measured 2026-07-27 — the reachability claim in BASELINE note 26u is NOT confirmed. On the
    // FIRMWARE the inherit is already pre-resolved at NV-load (`fw_main.cpp` writes nv.freq_mhz into BOTH
    // layers, and nv.freq_mhz is never 0), so a board provisioned via `gateway`/`cfg set` never actually
    // reaches a 2-layer node with layers[i].freq_mhz == 0. The reachable users of the inherit are the
    // SIMULATOR (map_layers defaults freq_mhz to 0), native tests, and any future/foreign config path
    // (`mutable_config()`, a remote-config verb) that leaves it 0 — which is exactly why the rule now lives
    // in ONE place instead of being duplicated by that fw_main pre-resolution.
    // ★ DEFAULT 0.0 = "no global carrier configured", which preserves the pre-fix behaviour EXACTLY: nothing
    // to reset to, so the radio stays where it was (DeviceHal::set_rx_freq / the sim HalAdapter both drop
    // mhz<=0). The one configuration where that would still HIDE a stale carrier — one layer overriding while
    // the other inherits nothing — is refused by validate_gateway_layers (GwValErr::freq_inherit_no_global).
    // Placed here (after dv_hop_cap, ahead of the `double duty_cycle` below) so it lands in the existing
    // 8-byte-alignment padding slot instead of opening a new one: +8 B to NodeConfig, not +16.
    double   radio_freq_mhz = 0.0;
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
    // Wave-4 antidote (schedule re-advertisement): a gateway re-emits its window SCHEDULE this often (duty-gated), so a
    // neighbour whose cached anchor went stale / boundary-degenerate re-anchors and stops phase-locking into a
    // never-opening window (the s15 cross-layer livelock). Emitted at window-activation => accurate offsets. 0 = OFF
    // (pre-Wave-4 reactive-only behaviour). Gateway-only; single-layer nodes never consult it (s18 inert).
    uint32_t gw_schedule_readvert_ms    = 300000;     // 5 min default
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
    // ★★ `loc_in_dm` DELETED 2026-07-31 (§loc-per-send, open-bug-register B0). It was a CONFIG TOGGLE that attached the
    // 6-B position to every originated app DM on a size check ALONE — with NO crypt gate — so a plaintext DM from a node
    // with `loc_in_dm = 1` aired the position IN THE CLEAR. The comment that used to sit on this line claimed
    // "(DATA_FLAG_LOCATION, sealed inner)", which was true only of a CRYPTED DM and is precisely why the leak read as
    // intended behaviour (V1 drift). ⇒ OWNER RULING (2026-07-30, twice): location is now a PER-SEND request — `send -l`
    // — carried in the existing DATA_FLAG_LOCATION bit of the command `flags` word, and REFUSED LOUD (never silently
    // omitted, never aired in clear) when the DM would not be sealed. The attach + the three refusals live at the one
    // same-layer origination choke point, Node::enqueue_data. `cfg set loc_dm`, the NV field and the app-facing binary
    // TLV are all gone with it. There is deliberately NO replacement field here: a per-send intent must not have a
    // persistent home, or the toggle grows back.
    bool     loc_in_m  = false;                  // piggyback location on originated channel M frames (flavor 0x08, public)
    bool     e2e_dm    = false;                  // Phase 1: originate app DMs ENCRYPTED when the recipient's pubkey is known; default OFF -> s18 byte-identical
    bool     intro_attach = true;                // §S2 D1 escape hatch: attach our pubkey (DATA_TYPE_INTRO) to a PLAINTEXT hash-addressed first-contact DM (no peer_confirmed(dst) yet). Default ON. OFF = never attach (the app must reqpubkey/QR-import). Inert without a crypto identity (_crypto_ready) -> s18 byte-identical.
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
    // §S0 defer-loop giveup: how many times this logical send has DRAINED (route appeared) then re-deferred (route
    // unusable at select — an aliased-mobile / gateway transit next-hop). Survives the drain->tx_queue->issue->defer
    // round trip on the TxItem (NOT reset by txitem_from_pending — it is defer meta, like requeue_count). defer_send
    // fails loud once it reaches send_defer_max_redrains, breaking the "re-drain every 1s forever" restamp loop.
    uint8_t  redrain_count = 0;
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
    // ★★★★★ §hybrid-rts S4c (2026-08-10) — ⛔⛔ **RENAMED FROM `data_ever_transmitted`, BECAUSE THAT NAME NAMED A
    // FACT THIS FLAG CANNOT ESTABLISH.** It means exactly ONE thing: **"a DM DATA for this pending copy was ADMITTED
    // to the HAL at least once"** (`tx_with_retry` answered `TxHandOff::handed`, i.e. `IHal::tx` returned `ok`).
    // ⛔ IT IS **NOT** "the DATA aired", and the gap is not theoretical in either direction:
    //   · **admitted-but-never-aired (FALSE POSITIVE).** `DeviceHal::tx` *"ENQUEUES … Returns ok when queued"*
    //     (`lib/hal/device_hal.cpp:10-12`); the on-air send happens later in `pump_tx()`. Two live rejections come
    //     AFTER a successful admission: (i) `Node::on_radio_busy(FrameTag::data)` (`node.cpp:2191-2195`) — the
    //     medium refused a frame this flag already booked; it clears `awaiting_ack` but **cannot unset this flag**,
    //     and the path is EXERCISED: **652 `data_tx_blocked` events across 15 of the 36 corpus scenarios**;
    //     (ii) `pump_tx()`'s failed arm DROPS the queued frame and does not retry it. §T2 now reports that attempt
    //     as `tx_failed`; it deliberately does not rewrite this admission fact.
    //   · **aired-but-never-recorded (FALSE NEGATIVE) = [[B164]]:** `duty_defer_fire`'s `handed && tag == data`
    //     re-hand and `retry_stashed`'s direct `_hal.tx` both fly a DATA for this same flight and write nothing.
    //     ⛔⛔ **§hybrid-rts S4d (2026-08-10) — THIS BULLET IS HALF-RETIRED IN PLACE.** The `duty_defer_fire` half
    //     was an **ADMISSION-side** false negative and it is CLOSED: that path re-enters `tx_with_retry`, which now
    //     carries the single write. `retry_stashed` still writes nothing and correctly needs nothing (argument at
    //     the crossing point). ⇒ what survives of this bullet is only *"admitted ⇏ aired"*, i.e. the FALSE-POSITIVE
    //     bullet above — the false-negative direction for **admission** no longer exists.
    // ⇒ ⛔ **THE FIELD IS EXACT ABOUT ADMISSION (S4d) AND NEVER PROOF OF AIRING.** ⓘ It read *"BEST-EFFORT ADMISSION
    //   TELEMETRY"* before S4d, which was accurate then and is too weak now. Downgrading the NAME (rather
    //   rather than conflating it with the flight-correlated completion signal) is a DELIBERATE choice recorded in
    //   `BASELINE.md` §HYBRID-RTS-S4c. §T2 now establishes and reports the separate true "aired" fact, but no
    //   protocol/app consumer uses it yet (T3 owns that), and both implicit-credit bases still take the SAME action.
    //   Therefore this field keeps its admission name and meaning; do not rename it or make completion write it.
    //   The split remains diagnostic only.
    //
    // ⛔⛔ §hybrid-rts S4d (2026-08-10) — **THE TEXT THAT STOOD HERE IS AMENDED, AND SO IS THE FLAG'S FALSE SIDE.**
    //   ⛔ WHAT IT SAID: *"Set at ONE place only: `do_data_tx`'s `disp == TxHandOff::handed` arm."* That was true and
    //   it was the bug: `do_data_tx` is the INITIAL send path only, while `duty_defer_fire` re-runs `tx_with_retry`
    //   from the stash when the duty wait expires and can obtain an admission there — with no write. ⇒ `true` meant
    //   "an admission was observed", but **`false` did NOT mean "no admission happened"**, and the consumer maps
    //   `false` **CATEGORICALLY** onto `basis=alternate_path` = *"we admitted none"* (design §5.2 item 2).
    //   ★★ NOW SET AT **ONE CROSSING POINT**: inside `Node::tx_with_retry`, immediately after `_hal.tx()` returns
    //   `TxResult::ok`, guarded on `tag == FrameTag::data && _active->_pending_tx && !m_broadcast`. **Every** DM-DATA
    //   admission — initial AND duty-deferred-retry — crosses that call, so the fact is now EXACT for admission in
    //   BOTH directions, and a future call path cannot bypass it. (A guarantee made at one of several exits is not
    //   made at all — the same structural lesson as [[B162]]'s refusal banner.)
    //   ★ `retry_stashed` calls `_hal.tx` directly and deliberately takes NO writer: reaching it requires a prior
    //   successful admission at the crossing point, and it only re-sends the stashed bytes of the frame that was
    //   admitted. The full two-direction argument is in-source at the crossing point. ⛔ Do NOT add a third copy.
    // ⛔ NOT before `_hal.tx()`: a duty-deferred (`deferred_retry_armed`) or HAL-refused (`rejected`) DATA was never
    //   even admitted, so booking it earlier would make the flag wrong about its OWN, weaker proposition. (That is
    //   the mutation the S4 gate runs, and it is still live.)
    // ⛔ AND NOT for an M-broadcast flight: that path airs an M frame (cmd 0xA), not a DATA frame, and its flight is
    //    excluded from the credit altogether — see the guard at `handle_rts`. ⚠ It reaches `tx_with_retry` WITH
    //    `FrameTag::data` (`node_mac.cpp:1788`), so the `!m_broadcast` term of the crossing-point guard is
    //    load-bearing, not decorative.
    // ⛔ WHAT S4d DOES **NOT** CHANGE: this field's AIRING meaning. The two post-admission drop mechanisms above
    //    stand and neither can unset it. §T2 reports their separate attempt outcomes; it does not repurpose the flag.
    //    ⇒ the honest one-liner remains **"exact about admission, silent about airing"**, while the app/UI use of the
    //    separate completion fact remains T3.
    //
    // WHO READS IT: `handle_rts`'s restored implicit-forward credit, and ONLY to LABEL the credit's basis —
    //   `local_admitted` (this node admitted a DATA for the flight to its own radio, so an exact downstream forward
    //   is CONSISTENT WITH the next hop having obtained it after our attempt) vs `alternate_path` (we admitted
    //   none, so the next hop got the same flight through another branch and this local copy is redundant).
    //   ★★ THE ACTION IS IDENTICAL ON BOTH BASES and the justification that carries it is `alternate_path`'s —
    //   **"the flight is progressing and this local copy is redundant"**, ⛔ never *"our DATA crossed the hop"*.
    //   With this flag downgraded to admission telemetry, that redundancy argument is the **ONLY** justification
    //   either basis has ever had. ⛔ NEITHER basis is delivery evidence and the field must never gate an app
    //   outcome — design §5.2 / owner ruling §1.10.
    //
    // ⚠⚠ SCOPE IS THIS `PendingTx`, DELIBERATELY, AND IT IS NOT COPIED BY `txitem_from_pending` BELOW.
    //   Design §5.2 words the fact as *"if this `PendingTx` has ADMITTED a DATA at least once"* (S4c amended the
    //   spec's operational text; it used to read *"has transmitted DATA at least once"*), and a requeue
    //   (cascade / gateway doorstep hold / long-busy) DESTROYS this PendingTx and re-issues a NEW one for a fresh
    //   next-hop attempt from which nothing has yet been admitted ⇒ `false` is the CORRECT value there, not a
    //   dropped field. `cascade_to_alt` mutates the carrier IN PLACE (same flight, same generation) and therefore
    //   KEEPS a true value, which is also right: we did admit a DATA for this flight, just at the previous hop.
    //   ⇒ ⛔ Do NOT "fix" this by adding a twin to `TxItem`: that would grow `TxItem` x cap_tx_queue for a fact
    //   whose only consumer is a diagnostic LABEL, and the U2 ledger below would then owe a copy. The field list
    //   in that ledger is derived from the two structs, so this note is the answer to "why is it missing".
    // ★ LAYOUT, MEASURED not inferred (compile-only reveal on the native flag set `-DMESHROUTE_NATIVE=1
    //   -DMR_N_LAYERS=2`): `is_gw_relay` sits at offset 346 and `sizeof(PendingTx)` is 352 with alignof 8, so
    //   bytes 347..351 are pure tail padding — this bool takes 347 and the hole shrinks to four. `sizeof(PendingTx)`
    //   stays 352, `std::optional<PendingTx>` stays 360, and `sizeof(Node)` does NOT move (221880). Fourteenth
    //   application of the padding-placement rule (see the node.h sizeof ledger).
    bool     data_ever_admitted = false;
};
// S1 (2026-07-04): the ONE place a TxItem is re-materialized from an in-flight PendingTx. Every
// requeue site (try_cascade_requeue / gateway_doorstep_hold / the long-busy same-hop requeue) MUST
// route through this so a field added to the identity+crypto set can never be forgotten at one site
// again — the field-drop class (H4/M7: `type`, `nonce_seed`) that made CRYPTED DMs undeliverable and
// typed frames deliver as junk. Copies the FULL shared core; site-specific meta (requeue_count,
// enqueue_time_ms, next_attempt_ms) is applied by the caller AFTER. When you add a shared field to
// BOTH TxItem and PendingTx, add its copy HERE (the single update point).
//   Shared core copied: origin, dst, ctr_lo, ctr, flags, type, inner[+inner_len], nonce_seed[8],
//   is_forward(<-has_previous_hop)/previous_hop, is_gw_relay, fwd_remaining, fwd_committed,
//   addr_len, mobile_src, plane.
//   NOT copied (site meta, set by the caller): requeue_count, enqueue_time_ms, next_attempt_ms.
// ★★ §B160 (2026-08-08) — HOW A FIELD WAS FORGOTTEN ANYWAY, AND WHY THE COMMENT ABOVE IS PART OF THE CAUSE.
// `plane` was added to BOTH carriers by Wave 2 and copied at the OTHER end of the round trip (issue_send,
// node_mac.cpp:937 `pt.plane = item.plane`) — but never here. So every requeue resurrected the flight as
// `Plane::AUTO`, and AUTO dispatches by `is_team_peer(dst)` (node.h:308): a GLOBAL flight whose dst numerically
// collides a teammate's team id came back as a TEAM flight — routed on `_rt_team`, and aired by tx_rts_retry
// (node_mac.cpp:1078) with `addr_len=1 / mobile_src=1 / src=team_local_id()`. That is EXACTLY the §team-parity
// T8 class (a static-plane flight wearing team-plane wire marks, breaching A2/I2), re-entering through the
// requeue door after T8 closed the origination door.
// ★ U2 IS A PROMISE, AND A PROMISE NOT KEPT IS WORSE THAN NO PROMISE. This header's own claim to be "the single
// update point" is what stopped anyone re-deriving the field list: a reader who trusts the claim audits the
// PROSE, not the struct. ⇒ when you add a shared field, DERIVE the set from `struct TxItem` / `struct PendingTx`
// and diff it against the assignments below — never against this comment. Three shared-by-name fields are STILL
// not copied here on purpose or by open defect, and only the struct diff will tell you which is which:
//   requeue_count, enqueue_time_ms  — site meta, deliberate (every caller sets them; see the list above).
//   flood / hop_left / flood_bitmap, and the semantic twin is_channel_m <- m_broadcast — OPEN, registered as
//     §B160-SIB in the bug register. Left alone here under C1 (this slice is the `plane` fix, not a second
//     behaviour change): an M/FLOOD flight sets awaiting_cts=awaiting_ack=false (node_mac.cpp:861) so it cannot
//     reach the two timeout requeue sites at all, and the third (the long-busy NACK arm) is reachable only via
//     a ctr_lo-aliased NACK on metal. Do NOT "just add them" without the test that proves the path.
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
    it.plane = pt.plane;                                                 // ★★ §B160: the addressing PLANE — without it a requeue resurrects
                                                                         // the flight as AUTO, so a GLOBAL flight to a team-colliding dst
                                                                         // comes back a TEAM flight (the T8 class via the requeue door)
    return it;
}
// §clean-join-carriers (2026-07-27, owner ruling) — THE ONE definition of "dropping this carrier strands an app
// future, so the drop owes a send_failed Push". Lives here, beside TxItem / PendingTx / DeferredSend, because it is a
// fact ABOUT them; all three call sites (purge_tx_carriers' queue + flight sweeps, clear_routing_state's _deferred
// wipe) read it, so the rule can never drift apart between them.
//   channel_m  — a channel M-broadcast (TxItem::is_channel_m / PendingTx::m_broadcast). It is fire-and-forget: the
//                app's future for a channel POST is the channel_sent Push, owned by the _channel_reoffer_pending slot,
//                NOT by any individual frame. Pushing send_failed per M would invent a future that never existed.
//   forwarded  — a TRANSIT frame (TxItem::is_forward / PendingTx::has_previous_hop): a relay leg, a home's last-mile
//                forward, or a gateway's cross-layer re-inject. Someone ELSE originated it; this node's app has no
//                future keyed on its (dst, ctr), and pushing would hand the companion a completion for a send it
//                never made — which can only collide with a real local (dst, ctr).
// Everything else staged or in flight IS an origination this node made on its own ctr — including a home
// re-originating for its hosted mobile, which is exactly how the existing giveup_flight path already treats it.
// ⛔⛔ THE QUESTION THIS ANSWERS IS NARROW, AND IT HAS ALREADY BEEN MISREAD ONCE. It answers *"does dropping this
//    carrier owe a `send_failed` push?"* — ⛔ NOT *"does this carrier own an app future?"*. The `channel_m` exclusion
//    exists because a channel post's future is a DIFFERENT one (`channel_sent`, owned by the re-offer slot), not
//    because there is none. §T3's `send_aired` design adopted this predicate for its ownership test on that
//    misreading; the result made `ChanState::aired` UNREACHABLE, because a canned post and an emergency both
//    transmit as channel-M. ⇒ §T3 uses TWO distinct ownership paths of its own (`Node::push_send_aired_if_owned`)
//    and ⛔ deliberately does NOT call this. Nothing here changes; this note exists so the next reader does not
//    repeat the reuse.
static inline bool carrier_owes_send_failed(bool channel_m, bool forwarded) { return !channel_m && !forwarded; }

struct DeferredSend {                // a send with no route yet — held until one appears (or TTL)
    TxItem   item;
    uint64_t deferred_at_ms = 0;     // for the send_defer_ttl giveup (TTL checked FIRST on drain)
};
struct PendingRx {                   // the receiver state awaiting DATA (one per node)
    uint8_t  from = 0, dst = 0, ctr_lo = 0, chosen_data_sf = 0, payload_len = 0;
    // §rts-cr (2026-07-27): the SENDER's coding rate, decoded from the RTS cr_adv bits (frame_codec.h). Sits
    // with chosen_data_sf/payload_len because those three are exactly the triple start_pending_rx_expiry needs
    // to size the DATA wait. Sizing it with OUR active_cr() under-waited whenever the sender's CR was heavier
    // (a gateway leaf at cr8 talking to leaves at cr5) and dropped a frame still in the air. Default 5 mirrors
    // NodeConfig::radio_cr's default; handle_rts always overwrites it from the frame. FITS EXISTING PADDING —
    // PendingRx stays 32 B, so sizeof(Node) does not move.
    uint8_t  sender_cr = 5;
    uint64_t set_at_ms = 0;
    uint64_t expiry_ms = 0;          // absolute DATA-wait expiry (for the BUSY_RX NACK busy_for calc)
    bool     claimed_e2e_ack = false;// the RTS carried RTS_FLAG_E2E_ACK (backstop DROP exempted). Verified at DATA-time:
                                     // if the DATA is NOT a DATA_TYPE_E2E_ACK the sender lied -> flag it (e2e_ack_spoof).
    bool     mobile_from = false;    // §mobile: the RTS was mobile_src -> `from` is a home-assigned LOCAL id, NOT a global
                                     // identity -> the DATA-time learn (node_mac_rx.cpp) MUST NOT install it in the static _rt.
    // ★★★ §hybrid-rts S2 (2026-08-08) — the plane the RTS DECLARED ON THE WIRE, `rts_wire_team_plane(r)`
    // (frame_codec.h) = `(addr_len == 1 && mobile_src)`. ⛔ NOT `team_addr_for_us(...)`, which is receiver-relative
    // ADDRESS admission and can be TRUE at the same time as `for_static_rts` on a host's `(1,0)` last-mile to a
    // hosted mobile whose local id collides with a team member's `_team_local_id`. Storing whichever predicate
    // matched would make the stored plane depend on WHO is listening; the wire declaration does not.
    // Consumed at DATA time (stored TEAM requires `for_team_data`, stored STATIC/GLOBAL requires `for_static_data`)
    // and echoed on the terminal CTS. Placed with the other two bools so it lands in the SAME run.
    bool     wire_team_plane = false;
    // ★★★ §hybrid-rts S2 — THE FLIGHT IDENTITY THE RTS CARRIED (dm_crypto.h; 3 B plaintext / 4 B crypted + its
    // domain). MANDATORY on every admitted unicast RTS — `parse_rts` cannot return a unicast frame without one.
    // The DATA that follows is validated against it (`handle_data`), and only a MATCH may deliver, ACK or seed the
    // completed-flight cache. ⛔ Never compared by prefix: `rts_flight_identity_equal` is the ONE comparator and
    // it is full-width + domain.
    RtsFlightIdentity id{};
};
struct PostAck {                     // deferred deliver/forward after the ACK airtime
    bool     pending = false;
    bool     is_forward = false;     // false => deliver (dst==self); true => forward
    // ★★ §hashbind-plane (2026-07-31): this DATA rode the TEAM plane — `for_team_data` (node_mac_rx.cpp, "addressed to
    // OUR team-plane id", stable across every hop of a team flight) carried forward, because do_post_ack runs AFTER
    // _pending_rx is reset and PostAck holds neither `next` nor `addr_len`, so the plane cannot be re-derived there.
    // Exact sibling of PendingRx::mobile_from above, and for the same reason: the ids in a team-plane frame are TEAM
    // LOCAL ids, so the deferred ingest MUST NOT install them in the static node_id-indexed planes (§18 / C3).
    // Read by the hash-bind ingest (on_hash_bind_response / on_hash_bind_snoop). false on every static/non-team frame,
    // so every static plane stays byte-inert. ★ LAYOUT, MEASURED not inferred (template-reveal on all six board
    // flag-sets + native): PostAck has NO tail padding (`ctr` makes it 2-aligned and 262 is already even), so this bool
    // costs it 262 -> 264 wherever it is placed — but `sizeof(Node)` does NOT move on ANY target (native 220656;
    // 116736 xiao_sx1262/xiao_esp32s3; 147352 gateway/gateway_esp32s3; 116704 both *_mobile), because the +2 lands in
    // padding LayerRuntime already had after `_post_ack`. Hence no D2 six-env escalation was owed for this slice.
    bool     team_plane = false;
    // B251: the accepted hosted-mobile transit leg already consumed a real TX-queue row before handle_data
    // returned. do_post_ack still owns the relay snoop/policy pass, but must not materialize a second TxItem.
    // This is accepted-flight state, never reconstructed from the current mobile registry.
    bool     forward_prequeued = false;
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
// ⛔ §B153 (2026-08-08): `struct LastAcked` and the per-layer `_last_acked_from` map it populated are DELETED.
// They backed the RTS-time `already_received` short-circuit, which is retired for the information-theoretic
// reason recorded at `already_received` in frame_codec.h: a 7-B RTS cannot prove message identity, so no
// terminal answer may be derived from one. The DATA-level `_seen_origins` dedup is the sole authority and
// needs no per-hop cache. ⛔ Do not reintroduce either.
// ⚠ SUPERSEDED IN PART BY §hybrid-rts S2 (2026-08-08), and the distinction is the whole point: `CompletedFlight`
//   below is NOT `LastAcked` restored. LastAcked was keyed `(src<<24|dst<<16|ctr_lo<<8|len)` — a 4-bit counter and
//   a LENGTH, which is precisely the key that could not tell a retry of A from a first attempt of B. The new
//   record is keyed by the FULL wire identity the 10/11-B RTS now carries, plus the wire-declared plane and the
//   identity's DOMAIN. Its TTL is derived (`completed_flight_cache_ttl_ms`), not the retired 10 s literal.
// ★★★ §hybrid-rts S2 — ONE COMPLETED FLIGHT, i.e. "I have already received, ACKed and adjudicated this exact
// message from this exact immediate sender". The cache match is the COMPLETE tuple (design §4.3):
//     immediate sender | dst | team/static plane | plaintext/encrypted domain | full identity
// ⛔ `from` is the ON-AIR immediate sender — the RTS's own `src`, carried through `PendingRx::from`. It is NEVER
//    `meta.src_hint`: that is the simulator's PHY oracle (-1 on hardware) and keying state from it was [[B156]]'s
//    sim/metal divergence. ⛔ `payload_len` is deliberately ABSENT: it is a frame-consistency/NAV field and was
//    never identity (design §2.4). Adding it back "for safety" would re-create a length-shaped identity.
struct CompletedFlight {
    uint64_t          expiry_ms = 0;      // absolute; 0 == EMPTY slot (a live entry always has expiry > 0)
    RtsFlightIdentity id{};               // domain + width + bytes — compared ONLY via rts_flight_identity_equal
    uint8_t           from = 0;           // the on-air immediate sender (RTS src)
    uint8_t           dst  = 0;
    bool              team_plane = false; // the WIRE-declared plane (rts_wire_team_plane), never a receiver predicate
};
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
