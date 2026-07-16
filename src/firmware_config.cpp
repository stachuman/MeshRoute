// MeshRoute — src/firmware_config.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The config/provisioning cluster (see firmware_config.h) moved VERBATIM from fw_main.cpp (cleanup 2026-07-14).
// Increment A: apply_radio_live + handle_cfg_set + gw_*_err_str/handle_gateway. Shared device state comes from
// fw_context.h; behaviour-preserving (only relocated — the fw_main `P::` alias becomes meshroute::protocol::).
#include "firmware_config.h"
#include "fw_context.h"              // g_radio, g_iradio, g_hal, g_node, g_identity, g_freq_mhz, g_tx_power, g_radio_ok, g_lat_e7/lon_e7, g_ble_*
#include "firmware_config_parse.h"   // mrfw::parse_sf_list
#include "protocol_constants.h"      // meshroute::protocol::* (preamble_sym, gateway_node_id_max, discovery_beacon_period_ms, leaf_name_max)
#include "leaf_config.h"             // meshroute::duty_to_bp/bp_to_duty/frac_to_bp/bp_to_frac/ms_to_u16
#include "admin_auth.h"              // meshroute::Identity, admin_key_from_password (handle_password)
#include "console_json.h"            // §S3: write_mobile_status/_gw/_net/_gw_end/_err (companion JSON for `mobile status`/`gateways`)
#include "device_rng.h"             // mrrng::fill (handle_create lineage mint)
#include <Arduino.h>                 // Print, F()
#include <cstdlib>                   // atoi/atof/atol/strtoul
#include <cstring>                   // strcmp/strlen/memcpy
#include <cstdio>                    // snprintf

namespace mrfw {

// Increment B internalized apply_radio_live: its only callers (handle_cfg_set + provision_apply_live) now both
// live in this TU, so it reverts to file-static (was header-exposed only to bridge to fw_main's provision_apply_live).
static void apply_radio_live(const mrnv::Blob& b, bool reconfig) {
    g_freq_mhz = b.freq_mhz;
    g_tx_power = b.tx_power;
    if (reconfig && g_radio_ok) {
        g_radio.standby();                                         // SX1262: RF/modulation params latch in STANDBY
        g_radio.setFrequency((float)b.freq_mhz);
        g_radio.setBandwidth((float)b.bw_hz / 1000.0f);
        g_radio.setCodingRate((uint8_t)b.cr);
        g_iradio.set_rx_sf((int)b.routing_sf);                     // setSpreadingFactor + re-arm RX (+ _rx_sf)
    }
    g_hal.configure(/*sf=*/(int16_t)b.routing_sf, /*bw_hz=*/(int32_t)b.bw_hz, /*cr=*/(int8_t)b.cr,
                    /*preamble=*/(int16_t)meshroute::protocol::preamble_sym, /*power=*/(int8_t)b.tx_power, /*busy_hold=*/100);
    g_node.set_radio_cfg((uint8_t)b.routing_sf, (uint32_t)b.bw_hz, (uint8_t)b.cr);
}

static void seed_blob_from_live(mrnv::Blob& b);   // fwd decl — defined below (with the provisioning block); handle_cfg_set's seed path calls it

// `cfg set <key> <value>` — ACCUMULATES onto the pending NV blob (so several sets + ONE reboot works), then
// applies LIVE to the running node where possible. RADIO knobs (freq/routing_sf|control_sf/bw/cr/tx_power) +
// MAC knobs (sf_list/lbt/beacon_ms) take effect NOW; node_id + duty need a reboot (identity / on_init budget).
// Extra protocol knobs (nav/nav_ignore/hop_cap/leaf_id/gateway) apply live but are NOT persisted yet (reboot reverts).
void handle_cfg_set(const char* args, Print& out) {
    char key[20]; size_t k = 0;
    while (args[k] && args[k] != ' ' && k < sizeof(key) - 1) { key[k] = args[k]; ++k; }
    key[k] = '\0';
    const char* val = (args[k] == ' ') ? (args + k + 1) : (args + k);
    if (!*val) { out.println(F("> cfg err bad_args")); return; }

    // `lat`/`lon` live in the IDENTITY record (/mrid) alongside `name`, NOT the config blob — handle early.
    // Input is decimal degrees (e.g. `cfg set lat 52.2297`); stored as int32 degrees×1e7. atof is fine on
    // newlib-nano (only float *printf* is broken). load_id preserves the seed + name + the other coord.
    if (!strcmp(key, "lat") || !strcmp(key, "lon")) {
        mrnv::IdBlob idb{};
        if (!mrnv::load_id(idb)) memcpy(idb.seed, g_identity.seed, sizeof idb.seed);   // no /mrid yet -> running seed
        const int32_t e7 = (int32_t)(atof(val) * 1e7);
        if (key[2] == 't') { idb.lat_e7 = e7; g_lat_e7 = e7; g_node.mutable_config().lat_e7 = e7; }   // "lat" (also LIVE)
        else               { idb.lon_e7 = e7; g_lon_e7 = e7; g_node.mutable_config().lon_e7 = e7; }   // "lon" (also LIVE)
        idb.magic = mrnv::kIdMagic; idb.version = mrnv::kIdVersion;
        out.println(mrnv::save_id(idb) ? F("> cfg ok (saved to /mrid)") : F("> cfg err nv_save_failed"));
        return;
    }

    // `name` lives in the IDENTITY record (/mrid), NOT the config blob — handle it separately + early.
    if (!strcmp(key, "name")) {
        mrnv::IdBlob idb{};
        if (!mrnv::load_id(idb)) memcpy(idb.seed, g_identity.seed, sizeof idb.seed);  // keep the RUNNING seed
        size_t l = strlen(val); if (l > sizeof idb.name) l = sizeof idb.name;
        memcpy(idb.name, val, l); idb.name_len = (uint16_t)l;
        idb.magic = mrnv::kIdMagic; idb.version = mrnv::kIdVersion;
        if (mrnv::save_id(idb)) { g_node.set_name(idb.name, static_cast<uint8_t>(idb.name_len)); out.println(F("> cfg ok name (saved to /mrid)")); }   // §1.3: live-update the core name (pubkey exchange + display)
        else out.println(F("> cfg err nv_save_failed"));
        return;
    }

    // Base = the PENDING NV blob so consecutive sets ACCUMULATE (else each snapshot reverts the others).
    // §cleanup 2026-07-15: the load-failed seed is unified with seed_blob_from_live (its field set is byte-identical
    // to the former inline block — verified — matching the 5 other save sites). The unconditional (re)stamp BELOW is
    // KEPT: it runs on the load-SUCCESS path too, upgrading a loaded older-version blob to kVersion (seed_blob_from_live
    // never runs when load() succeeds, so it can't do that).
    mrnv::Blob b{};
    if (!mrnv::load(b)) seed_blob_from_live(b);            // nothing persisted yet -> seed from the live config
    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;    // (re)stamp -> also upgrades a loaded v2 blob to v3

    // live = takes effect on the RUNNING node now (else reboot); radio = needs apply_radio_live; persist = write NV.
    // node-config knobs apply via mutable_config() (the MAC re-reads those each use). duty stays reboot (its
    // budget_ms is computed once at on_init); the extra protocol knobs are live-only (not in the NV blob yet).
    meshroute::NodeConfig& lc = g_node.mutable_config();
    bool live = true, reconfig = false, radio = false, persist = true;
    if      (!strcmp(key, "node_id")) {
        const int v = atoi(val);
#if MR_N_LAYERS >= 2   // gateway build: layer-0 node_id IS a gateway id (R6.3/G1: 1..16)
        if (v != 0 && (v < 1 || v > meshroute::protocol::gateway_node_id_max)) { out.println(F("> cfg err bad_value (gateway node_id 1..16; 0=unprovisioned)")); return; }
#else                  // normal build: 17..254 (1..16 reserved for gateways)
        if (v < 0 || v > 254 || (v >= 1 && v <= meshroute::protocol::gateway_node_id_max)) { out.println(F("> cfg err bad_value (node_id 0 or 17..254; 1..16 reserved for gateways)")); return; }
#endif
        b.node_id = (uint8_t)v; b.joined = 0; live = false;        // operator-pinned id -> NOT DAD-adopted (won't auto-yield)
    }
    else if (!strcmp(key, "freq"))                                     { const double f = atof(val);        // mirror join/create: 100..1000 MHz — out-of-band persists an RF-dead node
                                                                         if (f < 100.0 || f > 1000.0) { out.println(F("> cfg err bad_value (freq 100..1000 MHz)")); return; }
                                                                         b.freq_mhz = f;                      reconfig = radio = true; }
    // BENCH NOTE (2026-06-19): SF5 does NOT lock over-the-air on the tested SX1262 modules (XIAO Wio-SX1262 +
    // Heltec V3) — the receiver completes ZERO reception (`status` isr==tx, rx=0) at BW125 AND BW500, and bumping
    // the TX preamble 16→256 made no difference, while SF6/7/8+ work through this exact path. It's an SX1262 PHY
    // limit, NOT a protocol rule, so it is deliberately NOT enforced in lib/core/on_init (the sim's idealized radio
    // has no such floor). => the usable control-SF floor on this hardware is 6; don't set routing_sf=5 on these
    // modules. Left configurable (no hard guard) for future SF5-capable hardware. Ref: SX1262 DS §6.1.1.1.
    else if (!strcmp(key, "routing_sf") || !strcmp(key, "control_sf")) { b.routing_sf = (uint8_t)atoi(val); reconfig = radio = true; }
    else if (!strcmp(key, "bw"))                                       { const long bw = atol(val);         // `cfg set bw` is in Hz; join/create take kHz 7..500 -> mirror as 7000..500000 Hz (bw<=0 -> downstream div-by-zero)
                                                                         if (bw < 7000 || bw > 500000) { out.println(F("> cfg err bad_value (bw 7000..500000 Hz)")); return; }
                                                                         b.bw_hz = (uint32_t)bw;              reconfig = radio = true; }
    else if (!strcmp(key, "cr"))                                       { const int cr = atoi(val);          // LoRa coding rate 4/5..4/8 -> 5..8 (SX1262 setCodingRate range)
                                                                         if (cr < 5 || cr > 8) { out.println(F("> cfg err bad_value (cr 5..8)")); return; }
                                                                         b.cr = (uint8_t)cr;                  reconfig = radio = true; }
    else if (!strcmp(key, "tx_power")) {
        const int v = atoi(val);
        if (v < -9 || v > 22) { out.println(F("> cfg err bad_value (tx_power -9..22 dBm)")); return; }
        b.tx_power = (int8_t)v; radio = true;                         // live, but no radio re-tune
    }
    // --- node-config knobs: LIVE via mutable_config() (the MAC re-reads each field per use), + persisted ---
    else if (!strcmp(key, "sf_list"))    { b.allowed_sf_bitmap = parse_sf_list(val); lc.allowed_sf_bitmap = b.allowed_sf_bitmap;
                                           if (b.lineage_id) b.config_epoch = (uint16_t)(b.config_epoch >= 65534 ? 65534 : b.config_epoch + 1); }   // R6.3 §4.1: a managed leaf-field write bumps epoch (propagates on reboot); saturate (u16 wrap -> permanent de-sync)
    else if (!strcmp(key, "lbt"))        { b.lbt = atoi(val) != 0;            lc.lbt_enabled = (b.lbt != 0); }
    else if (!strcmp(key, "beacon_ms"))  { const long bms = atol(val);                          // floor at the discovery cadence: 0/too-small = airtime storm after reboot
                                           if (bms < (long)meshroute::protocol::discovery_beacon_period_ms) { out.println(F("> cfg err bad_value (beacon_ms >= 5000)")); return; }
                                           b.beacon_ms = (uint32_t)bms; lc.beacon_period_ms = b.beacon_ms; }
    else if (!strcmp(key, "duty"))       { b.duty = meshroute::bp_to_duty(meshroute::duty_to_bp(atof(val))); live = false;   // §5: quantize to the 0.01% wire step so the config_hash matches across nodes
                                           if (b.lineage_id) b.config_epoch = (uint16_t)(b.config_epoch >= 65534 ? 65534 : b.config_epoch + 1); }   // R6.3 §4.1: managed leaf-field write bumps epoch; saturate (u16 wrap -> permanent de-sync)
    // --- nav/hop tuning: LIVE-only (good defaults; reboot reverts) ---
    else if (!strcmp(key, "nav"))        { lc.nav_enabled    = atoi(val) != 0; persist = false; }
    else if (!strcmp(key, "intra_layer_relay")) { lc.intra_layer_relay = (atoi(val) != 0 || !strcmp(val, "on")); persist = false; }   // §gateway: LIVE-only (default OFF is the fix)
    else if (!strcmp(key, "host_mobiles"))     { lc.host_mobiles   = (atoi(val) != 0 || !strcmp(val, "on")); persist = false; }   // §mobile 2a: accept/host mobiles? LIVE-only (default ON; reverts on reboot — a mobile itself never hosts)
    else if (!strcmp(key, "nav_ignore")) { lc.nav_ignore_rts = atoi(val) != 0; persist = false; }
    else if (!strcmp(key, "hop_cap"))    { lc.dv_hop_cap = (uint8_t)atoi(val); persist = false; }
    // --- location piggyback: LIVE via mutable_config() + PERSISTED (NV v9). The lat/lon are set via `cfg set lat`/`lon` (-> /mrid). ---
    else if (!strcmp(key, "loc_in_dm"))  { b.loc_in_dm = (atoi(val) != 0 || !strcmp(val, "on") || !strcmp(val, "true")) ? 1 : 0; lc.loc_in_dm = (b.loc_in_dm != 0); }
    // --- E2E §4b: originate app DMs ENCRYPTED. LIVE via mutable_config() + PERSISTED (NV v10). A no-pubkey CRYPTED send
    //     fails loud (send_failed{no_pubkey}); the user provisions keys via `peerkey`/`reqpubkey`. Default off = plaintext. ---
    else if (!strcmp(key, "e2e_dm"))     { b.e2e_dm = (atoi(val) != 0 || !strcmp(val, "on") || !strcmp(val, "true")) ? 1 : 0; lc.e2e_dm = (b.e2e_dm != 0); }
    // --- gateway noise control (duty-cycle protection): LIVE via mutable_config() + PERSISTED (NV v11). The MAC reads
    //     both each window-activation, so no reboot needed. duty_pct clamps to 1..100; interval 0 keeps the prior value. ---
    else if (!strcmp(key, "gw_announce_pct"))      { int v = atoi(val); if (v < 1) v = 1; if (v > 100) v = 100; lc.gw_announce_duty_pct = (uint8_t)v; b.gw_announce_duty_pct = lc.gw_announce_duty_pct; }
    else if (!strcmp(key, "gw_announce_interval")) { lc.gw_announce_min_interval_ms = (uint32_t)atol(val);       b.gw_announce_min_interval_ms = lc.gw_announce_min_interval_ms; }
    else if (!strcmp(key, "gw_herd_slack"))        { int v = atoi(val); if (v < 1) v = 1; if (v > 255) v = 255; lc.gw_herd_slack = (uint8_t)v; b.gw_herd_slack = lc.gw_herd_slack; }   // §3e herd-spread slack (live; MAC re-reads)
    // --- anti-spam v2 promoted knobs (2026-07-03): LIVE via mutable_config() (the MAC re-reads each use) + PERSISTED
    //     (NV v16) + in the config_hash. A managed leaf's write bumps config_epoch (via leaf_config_write) so the
    //     change re-fingerprints + propagates via the C config frame. No reboot (not radio params); b.<field> mirrors
    //     the quantized lc.<field> so NV holds the same wire-quantized value the config_hash saw. ---
    else if (!strcmp(key, "active_fraction")) {                                // channel_active_fraction: a 0..1 fraction (quantized to the 0.01% wire step)
        lc.channel_active_fraction = meshroute::bp_to_frac(meshroute::frac_to_bp((float)atof(val)));
        b.channel_active_fraction = lc.channel_active_fraction; if (lc.lineage_id) g_node.leaf_config_write();
    }
    else if (!strcmp(key, "ch_min_ms")) {                                      // channel_min_interval_ms in ms (u16 on the wire; clamps to 65535)
        lc.channel_min_interval_ms = (uint32_t)meshroute::ms_to_u16((uint32_t)atol(val));
        b.channel_min_interval_ms = lc.channel_min_interval_ms; if (lc.lineage_id) g_node.leaf_config_write();
    }
    else if (!strcmp(key, "dm_min_ms")) {                                      // dm_min_interval_ms in ms (u16 on the wire; clamps to 65535)
        lc.dm_min_interval_ms = (uint32_t)meshroute::ms_to_u16((uint32_t)atol(val));
        b.dm_min_interval_ms = lc.dm_min_interval_ms; if (lc.lineage_id) g_node.leaf_config_write();
    }
    else if (!strcmp(key, "leaf_name")) {                                      // the LEAF name (in the config_hash + C frame) — NOT `name` (the node identity in /mrid); a rename bumps the epoch live
        uint8_t l = 0; while (val[l] && l < meshroute::protocol::leaf_name_max) { lc.leaf_name[l] = val[l]; b.leaf_name[l] = (uint8_t)val[l]; ++l; }
        lc.leaf_name_len = l; b.leaf_name_len = l; if (lc.lineage_id) g_node.leaf_config_write();
    }
    // --- role/topology: LIVE via mutable_config() + PERSISTED (NV v6 -> survives reboot) ---
    else if (!strcmp(key, "leaf_id"))      { lc.leaf_id = (uint8_t)atoi(val);                            b.leaf_id      = lc.leaf_id; }
    // `gateway` is NOT a cfg key — is_gateway is DERIVED = (n_layers==2) in on_init (a gateway is the dedicated
    // gateway BUILD, MR_GATEWAY_BUILD; non-configurable so the companion's reported `gateway` is reliable).
    else if (!strcmp(key, "gateway_only")) { lc.gateway_only = (atoi(val) != 0 || !strcmp(val, "true")); b.gateway_only = lc.gateway_only ? 1 : 0; }
    else if (!strcmp(key, "mobile"))       { lc.is_mobile    = (atoi(val) != 0 || !strcmp(val, "true")); b.is_mobile    = lc.is_mobile    ? 1 : 0; }
    else if (!strcmp(key, "team_id"))      { lc.team_id      = (uint32_t)strtoul(val, nullptr, 0); b.team_id = lc.team_id; }   // §mobile 6.1: JOIN a team (LIVE + persist; reboot-to-apply like `mobile`)
    else if (!strcmp(key, "mobile_autoregister")) { lc.mobile_autoregister = (atoi(val)!=0 || !strcmp(val,"true")); b.mobile_autoregister = lc.mobile_autoregister?1:0; }   // §mobile console: autonomy toggle (LIVE + persist)
    // --- BLE companion policy: PERSISTED, reboot-to-apply (the stack inits at boot from these). Invalid input
    //     is REJECTED (fail loud), never silently defaulted. ---
    else if (!strcmp(key, "ble_mode")) {
        uint8_t m;
        if      (!strcmp(val, "off"))      m = 0;
        else if (!strcmp(val, "on"))       m = 1;
        else if (!strcmp(val, "periodic")) m = 2;
        else { out.println(F("> cfg err bad_value (ble_mode off|on|periodic)")); return; }
        b.ble_mode = m; live = false;
    }
    else if (!strcmp(key, "ble_period")) {
        const int v = atoi(val);
        if (v < 1 || v > 255) { out.println(F("> cfg err bad_value (ble_period 1..255 min)")); return; }
        b.ble_period_min = (uint8_t)v; live = false;
    }
    else if (!strcmp(key, "ble_pin")) {
        const long v = atol(val);
        if (v < 0 || v > 999999) { out.println(F("> cfg err bad_value (ble_pin 0..999999, 6-digit passkey)")); return; }
        b.ble_pin = (uint32_t)v; live = false;
    }
    // --- v8 DUAL-LAYER GATEWAY: PERSISTED raw per-layer fields, reboot-to-apply (on_init validates + derives the
    //     window split). Invalid input is REJECTED (fail loud), never silently clamped/defaulted. layer 0 = the
    //     legacy node_id/routing_sf/sf_list/beacon_ms keys; these are the layer-1 + shared-schedule extras. ---
    else if (!strcmp(key, "n_layers")) {
        const int v = atoi(val);
        if (v != 1 && v != 2) { out.println(F("> cfg err bad_value (n_layers 1|2)")); return; }
        b.n_layers = (uint8_t)v; live = false;
    }
    else if (!strcmp(key, "layer0_id")) {
        const int v = atoi(val);
        if (v < 0 || v > 255) { out.println(F("> cfg err bad_value (layer0_id 0..255)")); return; }
        b.layer0_id = (uint8_t)v; live = false;
    }
    else if (!strcmp(key, "window_period_ms")) {
        const long v = atol(val);
        if (v < 1) { out.println(F("> cfg err bad_value (window_period_ms >= 1)")); return; }
        b.window_period_ms = (uint32_t)v; live = false;
    }
    else if (!strcmp(key, "l0_window_ms")) {
        const long v = atol(val);
        if (v < 0) { out.println(F("> cfg err bad_value (l0_window_ms 0=derive)")); return; }
        b.l0_window_ms = (uint32_t)v; live = false;
    }
    else if (!strcmp(key, "l0_window_offset_ms")) {
        const long v = atol(val);
        if (v < 0) { out.println(F("> cfg err bad_value (l0_window_offset_ms 0=derive)")); return; }
        b.l0_window_offset_ms = (uint32_t)v; live = false;
    }
    else if (!strcmp(key, "l1_layer_id")) {
        const int v = atoi(val);
        if (v < 0 || v > 255) { out.println(F("> cfg err bad_value (l1_layer_id 0..255)")); return; }
        b.l1_layer_id = (uint8_t)v; live = false;
    }
    else if (!strcmp(key, "l1_node_id")) {                          // R6.3/G1: the gateway's layer-1 id is also a gateway id (1..16)
        const int v = atoi(val);
        if (v != 0 && (v < 1 || v > meshroute::protocol::gateway_node_id_max)) { out.println(F("> cfg err bad_value (l1_node_id 1..16; 0=unprovisioned)")); return; }
        b.l1_node_id = (uint8_t)v; live = false;
    }
    else if (!strcmp(key, "l1_routing_sf")) {
        const int v = atoi(val);
        if (v < 5 || v > 12) { out.println(F("> cfg err bad_value (l1_routing_sf 5..12)")); return; }
        b.l1_routing_sf = (uint8_t)v; live = false;
    }
    else if (!strcmp(key, "l1_sf_list")) {
        const uint16_t bm = parse_sf_list(val);
        if (!bm) { out.println(F("> cfg err bad_value (l1_sf_list: comma SFs 5..12, e.g. 7,9)")); return; }
        b.l1_allowed_sf_bitmap = bm; live = false;
    }
    else if (!strcmp(key, "l1_beacon_ms")) {
        const long v = atol(val);
        if (v < 1) { out.println(F("> cfg err bad_value (l1_beacon_ms >= 1)")); return; }
        b.l1_beacon_period_ms = (uint32_t)v; live = false;
    }
    else if (!strcmp(key, "l1_window_ms")) {
        const long v = atol(val);
        if (v < 0) { out.println(F("> cfg err bad_value (l1_window_ms 0=derive)")); return; }
        b.l1_window_ms = (uint32_t)v; live = false;
    }
    else if (!strcmp(key, "l1_window_offset_ms")) {
        const long v = atol(val);
        if (v < 0) { out.println(F("> cfg err bad_value (l1_window_offset_ms 0=derive)")); return; }
        b.l1_window_offset_ms = (uint32_t)v; live = false;
    }
    else if (!strcmp(key, "l1_freq")) {                          // v12 per-layer freq: layer-1 RF carrier (0 = inherit layer 0/`freq`)
        const double f = atof(val);
        if (f < 0.0) { out.println(F("> cfg err bad_value (l1_freq MHz; 0=inherit)")); return; }
        b.l1_freq_mhz = f; live = false;
    }
    else if (!strcmp(key, "l1_bw")) {                            // v17 per-layer BW: layer-1 bandwidth Hz (0 = inherit the global bw)
        const long v = atol(val);
        if (v < 0) { out.println(F("> cfg err bad_value (l1_bw Hz; 0=inherit)")); return; }
        b.l1_bw_hz = (uint32_t)v; live = false;
    }
    else if (!strcmp(key, "l1_cr")) {                            // v17 per-layer CR: layer-1 coding-rate 5..8 (0 = inherit)
        const int v = atoi(val);
        if (v != 0 && (v < 5 || v > 8)) { out.println(F("> cfg err bad_value (l1_cr 5..8; 0=inherit)")); return; }
        b.l1_cr = (uint8_t)v; live = false;
    }
    else { out.print(F("> cfg err unknown_key ")); out.println(key); return; }

    if (persist && !mrnv::save(b)) { out.println(F("> cfg err nv_save_failed")); return; }
    if (radio && live) apply_radio_live(b, reconfig);
    out.print(F("> cfg ")); out.print(key); out.print('='); out.print(val);
    if      (!live)   out.println(F(" ok (reboot to apply)"));
    else if (persist) out.println(F(" ok (live + saved)"));
    else              out.println(F(" ok (live, not persisted)"));
}

// ---- `gateway` one-command provisioning ---------------------------------------------------------------------
// Parse + the SHARED §3.2 gate (validate_gateway_layers — identical to on_init's, so the console can never persist
// a config on_init would refuse), then map into the v10 NV blob and prompt a reboot. Touches ONLY gateway fields
// (radio/freq/tx_power/duty/etc. in the loaded blob are preserved); beacon cadence is preserved unless `beacon=` given.
#if MR_N_LAYERS >= 2
static const char* gw_parse_err_str(meshroute::GwParseErr e) {
    using E = meshroute::GwParseErr;
    switch (e) {
        case E::missing_l0:  return "missing l0=";
        case E::missing_l1:  return "missing l1=";
        case E::bad_l0:      return "bad l0 format (want level:node:ctrl_sf:data_sfs)";
        case E::bad_l1:      return "bad l1 format (want level:node:ctrl_sf:data_sfs)";
        case E::bad_leaf:    return "level out of range (1..255)";
        case E::bad_node:    return "node out of range (1..254)";
        case E::bad_ctrl_sf: return "ctrl_sf out of range (5..12)";
        case E::bad_data_sf: return "data SF list empty or out of range (5..12)";
        case E::bad_period:  return "period must be > 0";
        case E::bad_window:  return "win0=/win1= want ms:offset";
        case E::bad_beacon:  return "beacon must be > 0";
        case E::unknown_opt: return "unknown option";
        default:             return "ok";
    }
}
static const char* gw_val_err_str(meshroute::GwValErr e) {
    using E = meshroute::GwValErr;
    switch (e) {
        case E::bad_leaf:             return "level 0 not allowed";
        case E::bad_ctrl_sf:          return "ctrl_sf out of range (5..12)";
        case E::no_data_sf:           return "a layer has no data SF";
        case E::leaf_nibble_clash:    return "the two leaf nibbles (leaf & 0x0F) collide (byte-0 wire filter)";
        case E::period_mismatch:      return "the two window periods differ (must share one cycle)";
        case E::period_zero:          return "window period must be > 0";
        case E::window_degenerate:    return "derived window is 0 (bad SF mix?)";
        case E::window_zero:          return "a window is 0";
        case E::window_exceeds_period:return "a window exceeds the period";
        case E::window_overlap:       return "the two windows overlap";
        case E::window_too_long:      return "windows sum exceeds the period";
        case E::bad_bw:               return "per-layer bw not a valid SX1262 bandwidth (0=inherit)";
        case E::bad_cr:               return "per-layer cr out of range (5..8; 0=inherit)";
        default:                      return "ok";
    }
}
#endif
void handle_gateway(const char* args, Print& out) {
#if MR_N_LAYERS < 2
    (void)args;
    out.println(F("> gateway err not_gateway_build (flash the [env:gateway] -DMR_N_LAYERS=2 firmware)"));
#else
    using namespace meshroute;
    GatewayProvision g{};
    const GwParseErr pe = parse_gateway_cmd(args, g);
    if (pe != GwParseErr::ok) { out.print(F("> gateway err ")); out.println(gw_parse_err_str(pe)); return; }

    // Base = the PENDING blob so radio/freq/identity-adjacent fields survive; seed from the live config if none.
    // §cleanup 2026-07-15: this is a DELIBERATE SUBSET of seed_blob_from_live — do NOT unify it. The full helper also
    // seeds is_mobile/node_id/leaf_id/team_id/gw_announce_*/gw_herd_slack/lineage/leaf_name/l1_*; here those are left
    // 0 (b{}) and handle_gateway overwrites the gateway-relevant ones below. Switching to seed_blob_from_live would
    // change the PERSISTED bytes (verified): is_mobile flips 0->1 with NO boot-side !=0 guard (inert only because a
    // gateway build compiles mobile out) + gw_announce_*/gw_herd_slack persist non-zero. Behaviourally harmless on a
    // gateway, but NOT byte-identical, so it stays a subset (a hygiene unify would be a real state change).
    mrnv::Blob b{};
    if (!mrnv::load(b)) {
        const NodeConfig& nc = g_node.config();
        b.freq_mhz = g_freq_mhz; b.bw_hz = nc.radio_bw_hz; b.cr = nc.radio_cr; b.duty = nc.duty_cycle;
        b.tx_power = g_tx_power;  b.lbt = nc.lbt_enabled ? 1 : 0; b.beacon_ms = nc.beacon_period_ms;
        b.ble_mode = g_ble_mode; b.ble_period_min = g_ble_period_min; b.ble_pin = g_ble_pin;
        b.channel_active_fraction = nc.channel_active_fraction; b.channel_min_interval_ms = nc.channel_min_interval_ms; b.dm_min_interval_ms = nc.dm_min_interval_ms;   // v16 anti-spam per-leaf tunables
    }
    const uint32_t bw = b.bw_hz ? b.bw_hz : static_cast<uint32_t>(LORA_BW * 1000.0);
    const uint8_t  cr = b.cr    ? b.cr    : 5;
    const GwValErr ve = validate_gateway_layers(g.l0, g.l1, bw, cr);   // SAME gate on_init runs (derives windows)
    if (ve != GwValErr::ok) { out.print(F("> gateway err ")); out.println(gw_val_err_str(ve)); return; }

    b.n_layers = 2;
    b.layer0_id = g.l0.layer_id; b.node_id = g.l0.node_id; b.routing_sf = g.l0.routing_sf; b.allowed_sf_bitmap = g.l0.allowed_sf_bitmap;
    b.l1_layer_id = g.l1.layer_id; b.l1_node_id = g.l1.node_id; b.l1_routing_sf = g.l1.routing_sf; b.l1_allowed_sf_bitmap = g.l1.allowed_sf_bitmap;
    b.window_period_ms = g.l0.window_period_ms;
    b.l0_window_ms = g.l0.window_ms; b.l0_window_offset_ms = g.l0.window_offset_ms;
    b.l1_window_ms = g.l1.window_ms; b.l1_window_offset_ms = g.l1.window_offset_ms;
    b.gateway_only = g.gateway_only ? 1 : 0;
    if (g.beacon_ms) { b.beacon_ms = g.beacon_ms; b.l1_beacon_period_ms = g.beacon_ms; }   // else: preserve existing cadence
    if (g.l0.freq_mhz > 0.0) b.freq_mhz = g.l0.freq_mhz;     // v12 per-layer freq: freq0 sets the node/layer-0 carrier (else keep)
    b.l1_freq_mhz = g.l1.freq_mhz;                           // 0 = inherit layer 0's freq at boot
    b.bw_hz = (g.l0.bw_hz > 0) ? g.l0.bw_hz : bw;            // v17: bw0 sets the node/layer-0 BW (else keep the global); layer 0 inherits it
    b.cr    = (g.l0.cr    > 0) ? g.l0.cr    : cr;            //      cr0 sets the node/layer-0 CR
    b.l1_bw_hz = g.l1.bw_hz; b.l1_cr = g.l1.cr;              //      bw1/cr1 = layer-1 (0 = inherit)
    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;
    if (!mrnv::save(b)) { out.println(F("> gateway err nv_save_failed")); return; }

    out.print(F("> gateway OK — L0 leaf")); out.print(g.l0.layer_id); out.print(F(" id")); out.print(g.l0.node_id);
    out.print(F(" sf")); out.print(g.l0.routing_sf);
    out.print(F(" | L1 leaf")); out.print(g.l1.layer_id); out.print(F(" id")); out.print(g.l1.node_id);
    out.print(F(" sf")); out.print(g.l1.routing_sf);
    out.print(F(" | period")); out.print(g.l0.window_period_ms);
    out.print(F("ms: L0 ")); out.print(g.l0.window_ms); out.print(F("@")); out.print(g.l0.window_offset_ms);
    out.print(F(" / L1 ")); out.print(g.l1.window_ms); out.print(F("@")); out.print(g.l1.window_offset_ms);
    out.print(F(" | freq L0 ")); out.print(g.l0.freq_mhz > 0.0 ? g.l0.freq_mhz : (double)g_freq_mhz, 4);
    out.print(F(" / L1 ")); out.print(g.l1.freq_mhz > 0.0 ? g.l1.freq_mhz : (g.l0.freq_mhz > 0.0 ? g.l0.freq_mhz : (double)g_freq_mhz), 4);
    if (g.gateway_only) out.print(F(" | gateway_only"));
    out.println(F(" — reboot to apply"));
#endif
}

// Seed a fresh blob from the live config (so a save on a never-persisted node doesn't zero the non-provisioning fields).
static void seed_blob_from_live(mrnv::Blob& b) {
    const meshroute::NodeConfig& nc = g_node.config();
    b.freq_mhz = g_freq_mhz;        b.bw_hz = nc.radio_bw_hz;       b.beacon_ms = nc.beacon_period_ms;
    b.duty = nc.duty_cycle;         b.allowed_sf_bitmap = nc.allowed_sf_bitmap;
    b.routing_sf = nc.routing_sf;   b.cr = nc.radio_cr;
    b.lbt = nc.lbt_enabled ? 1 : 0; b.node_id = g_node.canonical_node_id();   b.tx_power = g_tx_power;
    b.is_gateway = nc.is_gateway ? 1 : 0; b.gateway_only = nc.gateway_only ? 1 : 0;
    b.is_mobile  = nc.is_mobile ? 1 : 0;  b.leaf_id      = nc.leaf_id;  b.team_id = nc.team_id; b.mobile_autoregister = nc.mobile_autoregister ? 1 : 0; b.team_local_id = g_node.team_local_id();   // §mobile: preserve team + autoreg + team-DAD id across create/join
    b.ble_mode   = g_ble_mode;            b.ble_period_min = g_ble_period_min;  b.ble_pin = g_ble_pin;
    b.loc_in_dm  = nc.loc_in_dm ? 1 : 0;  b.e2e_dm     = nc.e2e_dm ? 1 : 0;
    b.gw_announce_duty_pct = nc.gw_announce_duty_pct; b.gw_announce_min_interval_ms = nc.gw_announce_min_interval_ms;
    b.l1_freq_mhz = nc.layers[1].freq_mhz; b.gw_herd_slack = nc.gw_herd_slack;
    b.l1_bw_hz = nc.layers[1].bw_hz; b.l1_cr = nc.layers[1].cr;   // v17 per-layer BW/CR (0 = inherit)
    b.lineage_id = nc.lineage_id; b.config_epoch = nc.config_epoch; b.leaf_name_len = nc.leaf_name_len;
    for (uint8_t i = 0; i < nc.leaf_name_len && i < sizeof(b.leaf_name); ++i) b.leaf_name[i] = (uint8_t)nc.leaf_name[i];
    b.channel_active_fraction = nc.channel_active_fraction; b.channel_min_interval_ms = nc.channel_min_interval_ms; b.dm_min_interval_ms = nc.dm_min_interval_ms;   // v16 anti-spam per-leaf tunables
    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;   // ★ STAMP here so EVERY caller gets a VALID blob. Without it a save path that seeds (load failed / fresh chip) but forgets to re-stamp — e.g. handle_team — persists magic=0/version=0, which the next boot's load() REJECTS => the whole config resets to defaults (the `cfg set mobile 1` -> reboot -> mobile=0 bug). The other callers also re-stamp (harmless/redundant now).
}

// Apply a just-saved provisioning blob LIVE (no reboot): radio re-tune + membership + config + (re-)DAD. The four
// §2 sub-paths. do_dad=false only for `leave` (stays unprovisioned, idle awaiting a join).
static void provision_apply_live(const mrnv::Blob& b, bool do_dad) {
    apply_radio_live(b, /*reconfig=*/true);                                  // §2 radio: freq/bw/ctrl_sf live (sets routing_sf/bw/cr)
    meshroute::NodeConfig& lc = g_node.mutable_config();                     // §2 config: MAC re-reads these each use
    lc.leaf_id = b.leaf_id; lc.layers[0].layer_id = b.leaf_id; lc.layers[0].routing_sf = b.routing_sf;
    lc.allowed_sf_bitmap = b.allowed_sf_bitmap; lc.layers[0].allowed_sf_bitmap = b.allowed_sf_bitmap;
    lc.duty_cycle = b.duty;  lc.lineage_id = b.lineage_id;
    if (b.channel_active_fraction > 0.0f) lc.channel_active_fraction = b.channel_active_fraction;   // v16: 0 => keep the NodeConfig default
    if (b.channel_min_interval_ms)        lc.channel_min_interval_ms  = b.channel_min_interval_ms;
    if (b.dm_min_interval_ms)             lc.dm_min_interval_ms       = b.dm_min_interval_ms;
    lc.leaf_name_len = b.leaf_name_len;
    for (uint8_t i = 0; i < b.leaf_name_len && i < sizeof(lc.leaf_name); ++i) lc.leaf_name[i] = (char)b.leaf_name[i];
    g_node.reset_leaf_epoch_state(b.config_epoch);                          // config_epoch + _max_seen_epoch (fresh-lineage numbering)
    g_node.recompute_duty_budget();                                         // §2(b) duty enforcement live
    g_node.reset_join_for_reprovision(); lc.layers[0].node_id = 0;          // §2 membership: drop id + CLEAR _joined so the re-DAD actually runs (set_identity alone leaves _joined -> join no-ops)
    g_node.clear_routing_state();                                           // the old network's routes/bindings/schedules are stale -> wipe
    g_node.set_rediscover_pending(do_dad);                                  // join/create: restart discovery once the new id is adopted (NOT leave -> idle)
    if (do_dad) { meshroute::Command jc{}; jc.kind = meshroute::CmdKind::join; (void)g_node.on_command(jc); }   // re-DAD live (claim-after-listen -> J ~join_listen_ms later)
}

// `join layer=<1..255> freq=<MHz> bw=<kHz> sf=<5..12>` — set the radio floor + (re-)DAD; auto-pulls the leaf config (R6.2).
#if MR_N_LAYERS < 2   // §config-integrity: create/join are normal-node-only — compiled out on the gateway build (refused at dispatch)
void handle_join(const char* args, Print& out) {
    char buf[128]; size_t bn = 0; for (; args[bn] && bn < sizeof(buf) - 1; ++bn) buf[bn] = args[bn]; buf[bn] = '\0';
    double freq = 0, bwk = 0; long sf = 0, layer = 0; bool hf = false, hb = false, hs = false, hlv = false;   // bwk is kHz (FRACTIONAL — 62.5 / 41.67 / 31.25 are valid LoRa BWs)
    char* p = buf; char* k; char* v;
    while (kv_next(p, k, v)) {
        if      (v && !strcmp(k, "freq"))  { freq  = atof(v); hf = true; }
        else if (v && !strcmp(k, "bw"))    { bwk   = atof(v); hb = true; }
        else if (v && !strcmp(k, "sf"))    { sf    = atol(v); hs = true; }
        else if (v && !strcmp(k, "layer")) { layer = atol(v); hlv = true; }   // the full 1..255 layer id (wire leaf nibble = layer & 0x0F)
        else { out.print(F("> join err bad/unknown key: ")); out.println(k); goto usage; }
    }
    if (!(hf && hb && hs && hlv) || freq < 100.0 || freq > 1000.0 || bwk < 7 || bwk > 500 || sf < 5 || sf > 12 || layer < 1 || layer > 255) goto usage;
    {
        mrnv::Blob b{}; if (!mrnv::load(b)) seed_blob_from_live(b);
        b.magic = mrnv::kMagic; b.version = mrnv::kVersion;
        b.freq_mhz = freq; b.bw_hz = (uint32_t)(bwk * 1000.0 + 0.5); b.routing_sf = (uint8_t)sf;   // kHz->Hz, ROUNDED (62.5->62500, not 62000)
        b.leaf_id = (uint8_t)(layer & 0x0F); b.layer0_id = (uint8_t)layer;       // full layer id stored; leaf = layer & 0x0F (byte-0 wire filter)
        b.node_id = 0; b.joined = 0; b.lineage_id = 0; b.config_epoch = 0;       // unprovisioned -> DAD + adopt the leaf's lineage via pull
        if (!mrnv::save(b)) { out.println(F("> join err nv_save_failed")); return; }
        provision_apply_live(b, /*do_dad=*/true);
        out.print(F("> join layer=")); out.print((int)layer); out.print(F(" freq=")); out.print(freq, 3);
        out.print(F(" bw=")); out.print(b.bw_hz); out.print(F("Hz sf=")); out.print((int)sf);
        out.print(F(" (leaf ")); out.print((int)(layer & 0x0F)); out.println(F(") — DADing id + pulling config (live)"));
        return;
    }
usage:
    out.println(F("> join err usage: join layer=<1..255> freq=<MHz> bw=<kHz 7..500, fractional ok e.g. 62.5> sf=<5..12>   (leaf = layer & 0x0F)"));
}

// `create layer=<1..255> freq=<MHz> bw=<kHz> sf=<5..12> sf_list=<7,9> duty=<pct> name="<text>"
//         [active_fraction=<0..1>] [ch_min_ms=<ms>] [dm_min_ms=<ms>]` — join's floor + mint a MANAGED leaf (mother).
// The anti-spam keys are OPTIONAL: omitted => the protocol DEFAULTS (never inherited from the node's current settings).
void handle_create(const char* args, Print& out) {
    char buf[192]; size_t bn = 0; for (; args[bn] && bn < sizeof(buf) - 1; ++bn) buf[bn] = args[bn]; buf[bn] = '\0';
    double freq = 0, dutypct = -1, bwk = 0; long sf = 0, layer = 0; uint16_t sfbm = 0;   // bwk is kHz (FRACTIONAL — 62.5 / 41.67 / 31.25 are valid LoRa BWs)
    char nm[meshroute::protocol::leaf_name_max]; uint8_t nlen = 0;
    float af = 0.125f; long chi = meshroute::protocol::channel_min_interval_ms, dmi = meshroute::protocol::dm_min_interval_ms;   // anti-spam DEFAULTS (overridden only if the key is given)
    bool hf = false, hb = false, hs = false, hlv = false, hlist = false, hduty = false, hname = false;
    char* p = buf; char* k; char* v;
    while (kv_next(p, k, v)) {
        if      (v && !strcmp(k, "freq"))            { freq = atof(v); hf = true; }
        else if (v && !strcmp(k, "bw"))              { bwk = atof(v); hb = true; }
        else if (v && !strcmp(k, "sf"))              { sf = atol(v); hs = true; }
        else if (v && !strcmp(k, "layer"))           { layer = atol(v); hlv = true; }   // the full 1..255 layer id (wire leaf nibble = layer & 0x0F)
        else if (v && !strcmp(k, "sf_list"))         { sfbm = parse_sf_list(v); hlist = true; }
        else if (v && !strcmp(k, "duty"))            { dutypct = atof(v); hduty = true; }
        else if (v && !strcmp(k, "name"))            { for (const char* c = v; *c && nlen < sizeof(nm); ++c) nm[nlen++] = *c; hname = true; }
        else if (v && !strcmp(k, "active_fraction")) { af = (float)atof(v); }
        else if (v && !strcmp(k, "ch_min_ms"))       { chi = atol(v); }
        else if (v && !strcmp(k, "dm_min_ms"))       { dmi = atol(v); }
        else { out.print(F("> create err bad/unknown key: ")); out.println(k); goto usage; }
    }
    if (!(hf && hb && hs && hlv && hlist && hduty && hname)) goto usage;
    if (freq < 100.0 || freq > 1000.0 || bwk < 7 || bwk > 500 || sf < 5 || sf > 12 || layer < 1 || layer > 255 || sfbm == 0 || dutypct < 0.0 || dutypct > 100.0) goto usage;
    if (af <= 0.0f) af = 0.125f; if (af > 1.0f) af = 1.0f;                    // clamp; 0/absent -> the default
    if (chi < 1) chi = meshroute::protocol::channel_min_interval_ms; if (dmi < 1) dmi = meshroute::protocol::dm_min_interval_ms;
    {
        mrnv::Blob b{}; if (!mrnv::load(b)) seed_blob_from_live(b);
        b.magic = mrnv::kMagic; b.version = mrnv::kVersion;
        b.freq_mhz = freq; b.bw_hz = (uint32_t)(bwk * 1000.0 + 0.5); b.routing_sf = (uint8_t)sf;   // kHz->Hz, ROUNDED (62.5->62500, not 62000)
        b.leaf_id = (uint8_t)(layer & 0x0F); b.layer0_id = (uint8_t)layer;    // full layer id; leaf = layer & 0x0F
        b.allowed_sf_bitmap = sfbm;
        b.duty = meshroute::bp_to_duty(meshroute::duty_to_bp(dutypct / 100.0));   // §5: percent -> 0..1, quantized to the 0.01% wire step
        for (uint8_t i = 0; i < nlen; ++i) b.leaf_name[i] = (uint8_t)nm[i]; b.leaf_name_len = nlen;
        b.channel_active_fraction = meshroute::bp_to_frac(meshroute::frac_to_bp(af));   // EXPLICIT (or default) — NEVER inherited; quantized for hash parity
        b.channel_min_interval_ms = (uint32_t)meshroute::ms_to_u16((uint32_t)chi);
        b.dm_min_interval_ms      = (uint32_t)meshroute::ms_to_u16((uint32_t)dmi);
        uint16_t lin = 0; do { mrrng::fill(reinterpret_cast<uint8_t*>(&lin), sizeof lin); } while (lin == 0);   // mint a managed lineage (never 0)
        b.lineage_id = lin; b.config_epoch = 1; b.node_id = 0; b.joined = 0;      // a fresh managed leaf starts at epoch 1
        if (!mrnv::save(b)) { out.println(F("> create err nv_save_failed")); return; }
        provision_apply_live(b, /*do_dad=*/true);
        out.print(F("> create layer=")); out.print((int)layer); out.print(F(" lineage=")); out.print(lin);
        out.print(F(" (leaf ")); out.print((int)(layer & 0x0F)); out.print(F(") name=\""));
        for (uint8_t i = 0; i < nlen; ++i) out.print((char)b.leaf_name[i]);
        out.print(F("\" af=")); out.print(b.channel_active_fraction, 3);
        out.print(F(" ch_min=")); out.print(b.channel_min_interval_ms); out.print(F(" dm_min=")); out.print(b.dm_min_interval_ms);
        out.println(F(" — mother live"));
        return;
    }
usage:
    out.println(F("> create err usage: create layer=<1..255> freq=<MHz> bw=<kHz 7..500, fractional ok e.g. 62.5> sf=<5..12> sf_list=<e.g.7,9> duty=<pct, fractional ok e.g. 0.1> name=\"<text>\" [active_fraction=<0..1>] [ch_min_ms=<ms>] [dm_min_ms=<ms>]   (leaf = layer & 0x0F)"));
}

// §mobile 6.1: FNV-1a over (key_hash32 ‖ nonce) = the 32-bit team_id (team_fnv1a32, firmware_config_parse.h).
// `team new` = MINT a fresh team_id = hash(our key ‖ HW-RNG nonce). `team <id>` = JOIN an existing team. `team 0` = leave.
void handle_team(const char* args, Print& out) {
    while (*args == ' ') ++args;
    const meshroute::NodeConfig& c = g_node.config();
    uint32_t t;
    const char* phy_args = nullptr;
    if (!strncmp(args, "new", 3)) {
        uint32_t nonce = 0; g_hal.rand_bytes(reinterpret_cast<uint8_t*>(&nonce), 4);
        t = team_fnv1a32(g_node.key_hash32(), nonce);
        phy_args = args + 3;   // §mobile 6.4: `team new [freq=<MHz> sf=<5-12> bw=<kHz>]` — optional team PHY
    } else if (args[0]) {
        char* endp = nullptr;
        t = (uint32_t)strtoul(args, &endp, 0);
        phy_args = endp;   // §6.4: `team <id> [freq= sf= bw=]` — a JOIN can set the shared team PHY too (mirrors `team new`)
    } else {
        out.println(F("> team err usage: `team new [freq= sf= bw=]` (mint) | `team <id> [freq= sf= bw=]` (join) | `team 0` (leave)"));
        return;
    }
    mrnv::Blob b{}; if (!mrnv::load(b)) seed_blob_from_live(b);
    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;   // ★ was MISSING (the only save path without a stamp) -> a `team` command on a fresh/rejected blob persisted magic=0 => next boot's load() rejected it => the whole config reset (mobile=0). seed_blob_from_live now also stamps; this is belt-and-suspenders + matches the other 6 save paths.
    b.team_id = t;
    // §mobile 6.4 Fix 6: set the team PHY so teammates hear each other (AND a member can later register with a compatible
    // static network). Mirror `mobile register freq=`. Omitted -> keep the current PHY. Requires is_mobile (a team is mobile).
#if MR_FEAT_MOBILE
    if (phy_args && strstr(phy_args, "freq=") && c.is_mobile) {
        const char* fs = strstr(phy_args, "freq="); const char* ss = strstr(phy_args, "sf="); const char* bs = strstr(phy_args, "bw=");
        double freq = strtod(fs + 5, nullptr);
        int    sf   = ss ? atoi(ss + 3) : 0;
        double bw   = bs ? strtod(bs + 3, nullptr) : 125.0;   // FRACTIONAL kHz (62.5 valid)
        if (freq < 100.0 || freq > 1000.0 || sf < 5 || sf > 12 || bw < 7.0 || bw > 500.0) {
            out.println(F("> team new err: freq 100..1000 MHz, sf 5..12, bw 7..500 kHz")); return;
        }
        meshroute::LayerConfig phy{};
        phy.layer_id = c.leaf_id; phy.routing_sf = (uint8_t)sf; phy.freq_mhz = freq;
        phy.bw_hz = (uint32_t)(bw * 1000.0 + 0.5); phy.allowed_sf_bitmap = (uint16_t)(1u << sf);
        g_node.mobile_register_phy(phy);                       // retune the radio (+ kick the FSM -> team-DAD via the no-host path)
        b.freq_mhz = freq; b.routing_sf = (uint8_t)sf; b.bw_hz = phy.bw_hz; b.allowed_sf_bitmap = phy.allowed_sf_bitmap;   // PERSIST the team PHY
        out.print(F("> team PHY: freq=")); out.print(freq, 3); out.print(F(" sf=")); out.print(sf); out.print(F(" bw=")); out.print(bw, 2); out.println(F(" kHz"));
    }
#endif
    // §6.4: a team is a SHARED-PHY overlay — members can only hear each other on a COMMON freq/routing_sf/sf_list/bw, and an
    // empty sf_list blocks DATA entirely ([[data-sf-removed]]). Refuse to mint/join (t!=0) with an INCOMPLETE PHY so a member
    // never lands on an isolated island (the 250-vs-125 kHz / empty-sf_list state seen on the bench). Leave (t==0) is exempt.
    if (t != 0) {
        const double eff_freq = (c.is_mobile && c.layers[0].freq_mhz > 0.0) ? c.layers[0].freq_mhz : g_freq_mhz;
        if (eff_freq <= 0.0 || c.routing_sf < 5 || c.routing_sf > 12 || c.allowed_sf_bitmap == 0 || g_node.active_bw_hz() == 0) {
            out.println(F("> team err: incomplete PHY — need freq, routing_sf(5..12), sf_list(DATA SF), bw."));
            out.println(F(">   set them inline: `team new freq=869.0 sf=7 bw=125` — ALL members MUST use the SAME freq/sf/bw."));
            return;   // NOT joined/minted: team_id, _team_local_id, NV all unchanged
        }
    }
    const bool team_switched = (c.team_id != t);              // §6.4: capture BEFORE the mutable set (c is a live ref)
    if (team_switched) g_node.set_team_local_id(0);           // §6.4: leaving OR switching teams -> drop the stale team-DAD id (0 = left; a re-DAD picks a fresh one for the new team)
    g_node.mutable_config().team_id = t;                     // LIVE (team_dad_fire reads _cfg.team_id)
    if (c.is_mobile && t != 0 && team_switched) g_node.team_dad_fire();   // §6.4: bootstrap the team plane (self-assign a _team_local_id, no static host needed)
    b.node_id       = g_node.canonical_node_id();            // §6.4: team_dad_fire may have MOVED node_id (off-grid: node_id==team id) -> persist the live id, don't re-save the stale one loaded at entry
    b.team_local_id = g_node.team_local_id();                // §6.4: persist the fresh id (or 0 on leave) alongside team_id
    mrnv::save(b);                                            // PERSISTED
    char tx[9]; snprintf(tx, sizeof tx, "%08lX", (unsigned long)t);
    out.print(F("> team -> team_id=0x")); out.println(tx);
    if (c.is_mobile && t != 0) { out.print(F("  team-DAD: local_id=")); out.println(g_node.team_local_id()); }
}

// §mobile console: `mobile register [freq=<MHz> sf=<5-12> bw=<kHz> | scan]` · `gateways` · `query <gw>` · `status`.
#if MR_FEAT_MOBILE   // §featuresplit: the whole mobile console command compiles out on a static build (the FSM/accessors it drives are gone)
void handle_mobile(const char* args, Print& out) {
    while (*args == ' ') ++args;
    const meshroute::NodeConfig& c = g_node.config();
    if (!c.is_mobile) {   // §S3: JSON error (app-facing) — the whole `mobile` verb needs a mobile
        const size_t m = meshroute::console::write_mobile_err(s_inbox_jb, sizeof s_inbox_jb, "not_mobile");
        if (m) out.write(s_inbox_jb, m);
        return;
    }
    if (!strncmp(args, "register", 8)) {
        const char* p = args + 8; while (*p == ' ') ++p;
        if (!strncmp(p, "scan", 4)) {
            g_node.mobile_register_scan();
            out.print(F("> mobile register: scanning current + ")); out.print(g_node.learned_layers_count()); out.println(F(" known networks"));
        } else if (strstr(p, "freq=")) {
            const char* fs = strstr(p, "freq="); const char* ss = strstr(p, "sf="); const char* bs = strstr(p, "bw=");
            double freq = fs ? strtod(fs + 5, nullptr) : 0.0;
            int sf = ss ? atoi(ss + 3) : 0;
            double bw = bs ? strtod(bs + 3, nullptr) : 125.0;   // FRACTIONAL kHz — 62.5 / 41.67 / 31.25 are valid LoRa BWs (atof like join/create, NOT atoi which truncates 62.5->62)
            if (freq < 100.0 || freq > 1000.0 || sf < 5 || sf > 12 || bw < 7.0 || bw > 500.0) { out.println(F("> mobile register err: freq 100..1000 MHz, sf 5..12, bw 7..500 kHz")); return; }
            meshroute::LayerConfig phy{};
            phy.layer_id = c.leaf_id; phy.routing_sf = (uint8_t)sf; phy.freq_mhz = freq;
            phy.bw_hz = (uint32_t)(bw * 1000.0 + 0.5); phy.allowed_sf_bitmap = (uint16_t)(1u << sf);   // kHz->Hz ROUNDED (62.5->62500, not 62000)
            g_node.mobile_register_phy(phy);
            out.print(F("> mobile register: on freq=")); out.print(freq, 3); out.print(F(" sf=")); out.print(sf); out.print(F(" bw=")); out.print(bw, 2); out.println(F(" kHz"));
        } else {
            g_node.mobile_register_current();
            out.println(F("> mobile register: DISCOVER on the current PHY"));
        }
        return;
    }
    if (!strcmp(args, "gateways")) {   // §S3: streamed JSON — mobile_gw* then mobile_net* then mobile_gw_end (routes/routes_end pattern)
        uint8_t gws = 0;
        for (uint8_t i = 0; i < g_node.bridged_layer_cap(); ++i) {
            const auto& b = g_node.bridged_layer(i);
            if (!b.valid) continue;
            const size_t m = meshroute::console::write_mobile_gw(s_inbox_jb, sizeof s_inbox_jb, b.gw_id, b.dest_leaf);
            if (m) out.write(s_inbox_jb, m); ++gws;
        }
        const uint8_t nl = g_node.learned_layers_count();
        for (uint8_t i = 0; i < nl; ++i) {
            const auto& r = g_node.learned_layer(i);
            const size_t m = meshroute::console::write_mobile_net(s_inbox_jb, sizeof s_inbox_jb, r.layer_id,
                                 reinterpret_cast<const char*>(r.name), r.name_len, r.freq_khz, r.sf, r.bw_hz);
            if (m) out.write(s_inbox_jb, m);
        }
        const size_t m = meshroute::console::write_mobile_gw_end(s_inbox_jb, sizeof s_inbox_jb, gws, nl);
        if (m) out.write(s_inbox_jb, m);
        return;
    }
    if (!strncmp(args, "query", 5)) {
        const uint8_t gw = (uint8_t)strtoul(args + 5, nullptr, 0);
        if (!gw) { out.println(F("> mobile query err: usage 'mobile query <gw_id>'")); return; }
        g_node.mobile_send_layer_query(gw);
        out.print(F("> mobile query gw=")); out.print(gw); out.println(F(" sent (answer async -> 'mobile gateways')"));
        return;
    }
    if (!strcmp(args, "status")) {   // §S3: JSON status (integer kHz/Hz PHY block)
        meshroute::console::MobileStatusFields m{};
        m.registered = g_node.mobile_registered();
        if (m.registered) {
            m.home = g_node.mobile_home_id(); m.local = g_node.mobile_local_id();
            m.epoch = g_node.mobile_reg_epoch(); m.home_layer = g_node.mobile_home_layer();
        }
        m.autoregister = c.mobile_autoregister;
        m.layer   = c.layers[0].layer_id;
        const double pf = c.layers[0].freq_mhz > 0.0 ? c.layers[0].freq_mhz : g_freq_mhz;   // §mobile: live layer freq (fallback to boot/global if not yet adopted)
        m.freq_khz = static_cast<uint32_t>(pf * 1000.0 + 0.5);   // MHz double -> integer kHz (rounded; no float on the wire)
        m.sf      = c.routing_sf;
        m.bw_hz   = g_node.active_bw_hz();
        m.nets    = g_node.learned_layers_count();
        const size_t mm = meshroute::console::write_mobile_status(s_inbox_jb, sizeof s_inbox_jb, m);
        if (mm) out.write(s_inbox_jb, mm);
        return;
    }
    out.println(F("> mobile err usage: register [freq= sf= bw= | scan] | gateways | query <gw> | status"));
}
#endif   // MR_FEAT_MOBILE (handle_mobile)
#endif   // MR_N_LAYERS < 2 — handle_join / handle_create (normal-node provisioning)

// `leave` — wipe to default, keep ONLY freq; go unprovisioned + idle (the clean managed->managed re-join primitive).
void handle_leave(Print& out) {
    mrnv::Blob b{}; if (!mrnv::load(b)) seed_blob_from_live(b);
    const double keep_freq = b.freq_mhz;
    b = mrnv::Blob{};                                                        // zero everything...
    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;
    b.freq_mhz = keep_freq;                                                  // ...keep only freq
    b.bw_hz = (uint32_t)(LORA_BW * 1000.0); b.routing_sf = LORA_SF; b.cr = LORA_CR; b.tx_power = LORA_TX_POWER;
    b.beacon_ms = 900000; b.duty = (double)LORA_DUTY_CYCLE_PCT / 100.0;       // NodeConfig defaults (15 min, 10%)
    b.channel_active_fraction = 0.125f; b.channel_min_interval_ms = meshroute::protocol::channel_min_interval_ms; b.dm_min_interval_ms = meshroute::protocol::dm_min_interval_ms;   // v16 anti-spam per-leaf defaults
    if (!mrnv::save(b)) { out.println(F("> leave err nv_save_failed")); return; }
    provision_apply_live(b, /*do_dad=*/false);                              // unprovisioned + idle (no DAD)
    out.print(F("> left network (kept freq=")); out.print(keep_freq, 3); out.println(F(") — idle; `join` to re-provision (live)"));
}

#if MR_FEAT_REMOTE_MGMT
// `password <passphrase>` — LOCAL-ONLY (a dispatch verb; NEVER accepted over the mesh — remote_exec has no such verb).
// Derive the admin keypair (iterated-BLAKE2b -> identity_from_seed), pin admin_pubkey to NV, reset the replay floor,
// then discard the derived keypair (the credential lives in the operator's head, not the node — spec §2/§8).
void handle_password(const char* args, Print& out) {
    while (*args == ' ') ++args;
    size_t n = strlen(args);
    while (n && (args[n-1]=='\r' || args[n-1]=='\n' || args[n-1]==' ')) --n;
    if (n == 0) { out.println(F("> password err: usage `password <passphrase>` (local only)")); return; }
    meshroute::Identity admin{};
    out.println(F("> deriving admin key (a few seconds)..."));   // the KDF blocks; tell the operator it's not hung
    meshroute::admin_key_from_password(args, n, admin, []{ fw_wdt_feed(); });   // feed the WDT during the multi-second stretch
    g_node.admin_set_pubkey(admin.ed_pub);
    mrnv::Blob b{}; if (!mrnv::load(b)) seed_blob_from_live(b);
    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;
    for (int i = 0; i < 32; ++i) b.admin_pubkey[i] = admin.ed_pub[i];
    b.admin_provisioned = 1; b.admin_counter_floor = 0;      // fresh credential -> reset the replay floor
    const bool saved = mrnv::save(b);
    memset(&admin, 0, sizeof admin);                          // discard the derived keypair (best-effort local wipe)
    if (!saved) { out.println(F("> password err: nv_save_failed")); return; }
    out.print(F("> admin pubkey pinned (fp "));               // print only a 4-byte fingerprint, NEVER the pubkey/pw
    const uint8_t* pk = g_node.admin_pubkey();
    for (int i = 0; i < 4 && pk; ++i) { char hx[3]; snprintf(hx, sizeof hx, "%02X", pk[i]); out.print(hx); }
    out.println(F(")"));
}
#endif

}  // namespace mrfw
