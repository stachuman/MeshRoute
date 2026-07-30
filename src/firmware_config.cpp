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

// §bw-round-invariant (TU-wide, covers BOTH compile-time LORA_BW conversions in this file — the gateway-validate
// fallback in handle_gateway and the NV reset in handle_leave). Both now go through protocol::khz_to_hz (U1: one
// conversion path) instead of open-coding `(uint32_t)(LORA_BW * 1000.0)`. The helper ROUNDS where those sites
// TRUNCATED, so the switch is a no-op ONLY while the two agree for the configured LORA_BW — asserted here per env
// rather than claimed in a doc. A future board BW where they disagree FAILS THE BUILD, deliberately loud: it would
// otherwise silently shift a persisted NV value. Twin assert at the third site, src/fw_main.cpp (grep the tag).
static_assert(static_cast<uint32_t>(LORA_BW * 1000.0) == meshroute::protocol::khz_to_hz(LORA_BW),
              "§bw-round-invariant: truncating and rounding kHz->Hz disagree for this board's LORA_BW — "
              "routing it through khz_to_hz CHANGES the shipped value; re-gate that deliberately");

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
    g_node.set_radio_cfg((uint8_t)b.routing_sf, (uint32_t)b.bw_hz, (uint8_t)b.cr, b.freq_mhz);   // §layer-freq: carrier too — a stale _cfg.radio_freq_mhz would be re-pushed by the next gateway window switch
}

static void seed_blob_from_live(mrnv::Blob& b);   // fwd decl — defined below (with the provisioning block); handle_cfg_set's seed path calls it

// §nv-ritual (dedup 3-B item 4): the load-or-seed/stamp prologue every /mrcfg write path opened with, spelled out
// once. ★ Why the STAMP is the load-bearing half: `handle_team` once omitted it, so a `team` command on a fresh (or
// version-rejected) chip persisted magic=0/version=0 — which the next boot's load() REJECTS, reverting the WHOLE
// config to defaults (the `cfg set mobile 1` -> reboot -> mobile=0 bug). seed_blob_from_live stamps too, but only
// on the load-FAILED path; stamping here covers the load-SUCCEEDED path as well, which is what upgrades a loaded
// older-version blob to kVersion so a reflash MIGRATES instead of resetting.
// ⚠ The SAVE half is deliberately NOT wrapped: the seven call sites differ in failure handling (return / report and
// carry on because the live state is already applied / commit a persistence tracker only on success / skip the save
// entirely) and each prints a different user-visible string. Folding that into parameters would hide the very
// differences that matter. Sites that ignore the save result at all are tagged `§nv-unchecked` where they live.
void nv_load_stamped(mrnv::Blob& b) {
    if (!mrnv::load(b)) seed_blob_from_live(b);            // nothing persisted (or a rejected version) -> the live config
    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;    // (re)stamp on BOTH paths — also upgrades an older loaded blob
}

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
    // §nv-ritual: load-or-seed + stamp (the unconditional (re)stamp also upgrades a loaded older-version blob).
    mrnv::Blob b{}; nv_load_stamped(b);

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
                                                                         if (!valid_freq_mhz(f)) { out.println(F("> cfg err bad_value (freq 100..1000 MHz)")); return; }
                                                                         b.freq_mhz = f;                      reconfig = radio = true; }
    // BENCH NOTE (2026-06-19): SF5 does NOT lock over-the-air on the tested SX1262 modules (XIAO Wio-SX1262 +
    // Heltec V3) — the receiver completes ZERO reception (`status` isr==tx, rx=0) at BW125 AND BW500, and bumping
    // the TX preamble 16→256 made no difference, while SF6/7/8+ work through this exact path. It's an SX1262 PHY
    // limit, NOT a protocol rule, so it is deliberately NOT enforced in lib/core/on_init (the sim's idealized radio
    // has no such floor). => the usable control-SF floor on this hardware is 6; don't set routing_sf=5 on these
    // modules. Left configurable (no hard guard) for future SF5-capable hardware. Ref: SX1262 DS §6.1.1.1.
    // §3-A.2: the LoRa DOMAIN 5..12 IS enforced (junk/atoi-0 would persist an RF-dead node) — only the SF6 FLOOR above is waived.
    else if (!strcmp(key, "routing_sf") || !strcmp(key, "control_sf")) { const int v = atoi(val);
                                                                         if (!valid_routing_sf(v)) { out.println(F("> cfg err bad_value (routing_sf 5..12)")); return; }
                                                                         b.routing_sf = (uint8_t)v; reconfig = radio = true; }
    else if (!strcmp(key, "bw"))                                       { const double bwk = atof(val);      // W2b unit unification: kHz ALWAYS (fractional ok, e.g. 62.5) — mirrors join/create/gateway; kHz->Hz ROUNDED. BREAKING: was Hz. (bw<=0 -> downstream div-by-zero)
                                                                         if (!valid_bw_khz(bwk)) { out.println(F("> cfg err bad_value (bw 7..500 kHz, fractional ok e.g. 62.5)")); return; }
                                                                         b.bw_hz = meshroute::protocol::khz_to_hz(bwk); reconfig = radio = true; }
    else if (!strcmp(key, "cr"))                                       { const int cr = atoi(val);          // LoRa coding rate 4/5..4/8 -> 5..8 (SX1262 setCodingRate range)
                                                                         if (cr < 5 || cr > 8) { out.println(F("> cfg err bad_value (cr 5..8)")); return; }
                                                                         b.cr = (uint8_t)cr;                  reconfig = radio = true; }
    else if (!strcmp(key, "tx_power")) {
        const int v = atoi(val);
        if (v < -9 || v > 22) { out.println(F("> cfg err bad_value (tx_power -9..22 dBm)")); return; }
        b.tx_power = (int8_t)v; radio = true;                         // live, but no radio re-tune
    }
    // --- node-config knobs: LIVE via mutable_config() (the MAC re-reads each field per use), + persisted ---
    else if (!strcmp(key, "sf_list"))    { const uint16_t bm = parse_sf_list(val);   // §3-A.7: fail-loud grammar — ANY invalid entry rejects the whole list (an empty bitmap would block DATA entirely)
                                           if (!bm) { out.println(F("> cfg err bad_value (sf_list: comma SFs 5..12, e.g. 7,9 — whole list rejected on any invalid entry)")); return; }
                                           b.allowed_sf_bitmap = bm; lc.allowed_sf_bitmap = bm;
                                           if (b.lineage_id) b.config_epoch = (uint16_t)(b.config_epoch >= 65534 ? 65534 : b.config_epoch + 1); }   // R6.3 §4.1: a managed leaf-field write bumps epoch (propagates on reboot); saturate (u16 wrap -> permanent de-sync)
    else if (!strcmp(key, "lbt"))        { b.lbt = atoi(val) != 0;            lc.lbt_enabled = (b.lbt != 0); }
    else if (!strcmp(key, "beacon_ms"))  { const long bms = atol(val);                          // floor at the discovery cadence: 0/too-small = airtime storm after reboot
                                           if (bms < (long)meshroute::protocol::discovery_beacon_period_ms) { out.println(F("> cfg err bad_value (beacon_ms >= 5000)")); return; }
                                           b.beacon_ms = (uint32_t)bms; lc.beacon_period_ms = b.beacon_ms; }
    else if (!strcmp(key, "duty"))       { const double dpct = atof(val);   // W2b unit unification: PERCENT (1 = 1%, fractional ok e.g. 0.1 = 0.1%) — SAME unit + conversion as `create duty=`. BREAKING: was a raw 0..1 fraction.
                                           if (dpct < 0.0 || dpct > 100.0) { out.println(F("> cfg err bad_value (duty percent 0..100; 1 = 1%, fractional ok e.g. 0.1)")); return; }
                                           b.duty = meshroute::bp_to_duty(meshroute::duty_to_bp(dpct / 100.0)); live = false;   // §5: percent -> 0..1 fraction, quantized to the 0.01% wire step so the config_hash matches across nodes
                                           if (b.lineage_id) b.config_epoch = (uint16_t)(b.config_epoch >= 65534 ? 65534 : b.config_epoch + 1); }   // R6.3 §4.1: managed leaf-field write bumps epoch; saturate (u16 wrap -> permanent de-sync)
    // --- nav/hop tuning: LIVE-only (good defaults; reboot reverts) ---
    else if (!strcmp(key, "nav"))        { lc.nav_enabled    = atoi(val) != 0; persist = false; }
    else if (!strcmp(key, "intra_layer_relay")) { lc.intra_layer_relay = (atoi(val) != 0 || !strcmp(val, "on")); persist = false; }   // §gateway: LIVE-only (default OFF is the fix)
    else if (!strcmp(key, "host_mobiles"))     { lc.host_mobiles   = (atoi(val) != 0 || !strcmp(val, "on")); persist = false; }   // §mobile 2a: accept/host mobiles? LIVE-only (default ON; reverts on reboot — a mobile itself never hosts)
    else if (!strcmp(key, "nav_ignore")) { lc.nav_ignore_rts = atoi(val) != 0; persist = false; }
    else if (!strcmp(key, "hop_cap"))    { const int v = atoi(val);   // §3-A.2: protocol domain 1..16 — dv_hop_cap is the F RREQ TTL (codec: "config caps ttl <= 16") + the DV merge cap; flood_hop_max=16 clamps every flood horizon; 0 would kill ALL route learning
                                           if (!valid_hop_cap(v)) { out.println(F("> cfg err bad_value (hop_cap 1..16)")); return; }
                                           lc.dv_hop_cap = (uint8_t)v; persist = false; }
    // §team-parity T3: the TEAM plane's twin of `hop_cap`. Mirrors dv_hop_cap's surface EXACTLY and no further — same
    // valid_hop_cap domain (U1: the F RREQ TTL, the RREP 2x backstop and the DV merge cap are the same wire bounds on
    // either plane) and the same `persist = false` (LIVE-only, reboot reverts to protocol::team_hop_cap = 8), so there
    // is no NV blob and no J-frame field to extend (C4: no wire change). Refuses loud on a bad value (C2) — never clamps.
    // Not MR_FEAT_TEAM-gated, matching NodeConfig::team_hop_cap itself (node_carriers.h:141): the field exists on every
    // profile, so the key stays settable on a gateway build where it is simply inert.
    else if (!strcmp(key, "team_hop_cap")) { const int v = atoi(val);
                                           if (!valid_hop_cap(v)) { out.println(F("> cfg err bad_value (team_hop_cap 1..16)")); return; }
                                           lc.team_hop_cap = (uint8_t)v; persist = false; }
    // --- location piggyback: LIVE via mutable_config() + PERSISTED (NV v9). The lat/lon are set via `cfg set lat`/`lon` (-> /mrid). ---
    else if (!strcmp(key, "loc_in_dm"))  { b.loc_in_dm = (atoi(val) != 0 || !strcmp(val, "on") || !strcmp(val, "true")) ? 1 : 0; lc.loc_in_dm = (b.loc_in_dm != 0); }
    // --- E2E §4b: originate app DMs ENCRYPTED. LIVE via mutable_config() + PERSISTED (NV v10). A no-pubkey CRYPTED send
    //     fails loud (send_failed{no_pubkey}); the user provisions keys via `peerkey`/`reqpubkey`. Default off = plaintext. ---
    else if (!strcmp(key, "e2e_dm"))     { b.e2e_dm = (atoi(val) != 0 || !strcmp(val, "on") || !strcmp(val, "true")) ? 1 : 0; lc.e2e_dm = (b.e2e_dm != 0); }
    // --- §S2 first-contact INTRO auto-attach: LIVE via mutable_config() + PERSISTED (NV v21). Default ON. OFF = never
    //     attach our pubkey to a plaintext first send (the app must reqpubkey/QR-import). Mirrors e2e_dm/mobile_autoregister. ---
    else if (!strcmp(key, "intro_attach")) { b.intro_attach = (atoi(val) != 0 || !strcmp(val, "on") || !strcmp(val, "true")) ? 1 : 0; lc.intro_attach = (b.intro_attach != 0); }
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
    else if (!strcmp(key, "leaf_id"))      { const int v = atoi(val);   // §3-A.2: wire domain 0..15 — leaf_id rides ONLY the cmd-byte low nibble (wire::flags_of = b & 0x0F) on every leaf-filtered frame; >15 could never match ANY received frame (the node goes filter-deaf). join/create already store layer & 0x0F.
                                             if (!valid_leaf_id(v)) { out.println(F("> cfg err bad_value (leaf_id 0..15 — the wire leaf nibble; set the full layer id via join/create layer=)")); return; }
                                             lc.leaf_id = (uint8_t)v;                                    b.leaf_id      = lc.leaf_id; }
    // `gateway` is NOT a cfg key — is_gateway is DERIVED = (n_layers==2) in on_init (a gateway is the dedicated
    // gateway BUILD, MR_GATEWAY_BUILD; non-configurable so the companion's reported `gateway` is reliable).
    else if (!strcmp(key, "gateway_only")) { lc.gateway_only = (atoi(val) != 0 || !strcmp(val, "true")); b.gateway_only = lc.gateway_only ? 1 : 0; }
    else if (!strcmp(key, "mobile"))       { lc.is_mobile    = (atoi(val) != 0 || !strcmp(val, "true")); b.is_mobile    = lc.is_mobile    ? 1 : 0; }
    // §mobile 6.1: JOIN a team (LIVE + persist; reboot-to-apply like `mobile`). §clean-team (2026-07-27): routed through
    // the SAME core switch as `team new`/`team <id>` — this raw key was the SECOND live team-switch path and cleared
    // nothing at all, not even the stale team-DAD id. b.team_local_id must be re-read after the switch or a reboot would
    // resurrect the OLD team's id as CONFIRMED on the new team. (No team_dad_fire here — as before, this key is
    // reboot-to-apply; a mobile's FSM tick re-DADs on _team_local_id==0, node_mobile.cpp:30.)
    else if (!strcmp(key, "team_id"))      { (void)g_node.set_team_id((uint32_t)strtoul(val, nullptr, 0));
                                             b.team_id = lc.team_id; b.team_local_id = g_node.team_local_id(); }
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
    else if (!strcmp(key, "l1_bw")) {                            // v17 per-layer BW in kHz (W2b unit unification: kHz ALWAYS, fractional ok e.g. 62.5; 0 = inherit the global bw). Mirrors `gateway bw1=`. BREAKING: was Hz.
        const double bwk = atof(val);
        if (bwk < 0.0 || (bwk > 0.0 && !valid_bw_khz(bwk))) { out.println(F("> cfg err bad_value (l1_bw 7..500 kHz, fractional ok; 0=inherit)")); return; }   // 0 = inherit is the ONE exemption from the shared domain
        b.l1_bw_hz = meshroute::protocol::khz_to_hz(bwk); live = false;
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
// §w4-switchenum (2026-07-26): NO `default:` — `-Wswitch` only guards a default-LESS switch, and the missing
// `bad_freq` case used to fall into `default: return "ok"`, so a REFUSED `gateway freq0=0` printed
// "> gateway err ok" (a false success reported to the operator). Every GwParseErr is cased; the trailing return
// is unreachable and deliberately NOT "ok", so a future enumerator can never re-render a refusal as success.
static const char* gw_parse_err_str(meshroute::GwParseErr e) {
    using E = meshroute::GwParseErr;
    switch (e) {
        case E::ok:          return "ok";               // never printed: the caller only maps NON-ok (handle_gateway below)
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
        case E::bad_freq:    return "freq0=/freq1= must be > 0 (MHz)";
        case E::unknown_opt: return "unknown option";
    }
    return "unknown parse error";                       // unreachable (all GwParseErr cased) — fail LOUD, never "ok"
}
// §w4-switchenum: same treatment as gw_parse_err_str above. This one's table was COMPLETE (13 of 13 non-ok
// values mapped) — but it was correct only by luck: the `default: return "ok"` meant a 14th enumerator would
// have rendered the next refusal as success, exactly the way gw_parse_err_str's bad_freq did.
static const char* gw_val_err_str(meshroute::GwValErr e) {
    using E = meshroute::GwValErr;
    switch (e) {
        case E::ok:                   return "ok";      // never printed: the caller only maps NON-ok
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
        case E::freq_inherit_no_global: return "one layer sets freqN= and the other inherits, but no node carrier is set (`cfg set freq`)";
    }
    return "unknown validation error";                  // unreachable (all GwValErr cased) — fail LOUD, never "ok"
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
    // Same verdict against the §nv-ritual prologue (dedup 3-B item 4): nv_load_stamped() IS load-or-seed_blob_from_live,
    // so adopting it here would make exactly the persisted-byte change this comment refuses. The stamp stays inline below.
    mrnv::Blob b{};
    if (!mrnv::load(b)) {
        const NodeConfig& nc = g_node.config();
        b.freq_mhz = g_freq_mhz; b.bw_hz = nc.radio_bw_hz; b.cr = nc.radio_cr; b.duty = nc.duty_cycle;
        b.tx_power = g_tx_power;  b.lbt = nc.lbt_enabled ? 1 : 0; b.beacon_ms = nc.beacon_period_ms;
        b.ble_mode = g_ble_mode; b.ble_period_min = g_ble_period_min; b.ble_pin = g_ble_pin;
        b.channel_active_fraction = nc.channel_active_fraction; b.channel_min_interval_ms = nc.channel_min_interval_ms; b.dm_min_interval_ms = nc.dm_min_interval_ms;   // v16 anti-spam per-leaf tunables
    }
    const uint32_t bw = b.bw_hz ? b.bw_hz : meshroute::protocol::khz_to_hz(LORA_BW);   // §bw-round-invariant
    const uint8_t  cr = b.cr    ? b.cr    : 5;
    // §layer-freq: the global carrier THIS provisioning will boot with — `freq0=` becomes b.freq_mhz below
    // (that is how the console spells "the node/layer-0 carrier"), else the blob's existing freq, else the
    // build default. Passing the same value on_init will see keeps the two gates identical (anti-drift).
    const double   fq = (g.l0.freq_mhz > 0.0) ? g.l0.freq_mhz
                                              : (b.freq_mhz > 0.0 ? b.freq_mhz : (double)LORA_FREQ);
    const GwValErr ve = validate_gateway_layers(g.l0, g.l1, bw, cr, fq);   // SAME gate on_init runs (derives windows)
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

// §team-ch-key (T-K1): THE one node->Blob conversion for the team channel keypair (U2 — never rebuild a carrier
// field-by-field per site). Two callers: seed_blob_from_live (the load-FAILED path, so a create/join reprovision
// on a fresh chip does not silently drop the key) and handle_team (which must persist a pair it just
// minted/adopted). A keyless node writes present=0 + all-zero, which is exactly what load restores as "no key".
// NB it reads through the ACCESSORS, which return nullptr while keyless — so there is no path where an
// unflagged buffer leaks into NV as if it were a key.
static void blob_take_team_channel_key(mrnv::Blob& b) {
    const uint8_t* pub  = g_node.team_channel_pub();
    const uint8_t* priv = g_node.team_channel_priv();
    b.team_ch_key_present = (pub && priv) ? 1 : 0;
    for (uint8_t i = 0; i < 32; ++i) {
        b.team_ch_pub[i]  = pub  ? pub[i]  : 0;
        b.team_ch_priv[i] = priv ? priv[i] : 0;
    }
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
    b.intro_attach = nc.intro_attach ? 1 : 0;   // v21 §S2: preserve the first-contact INTRO toggle across create/join
    blob_take_team_channel_key(b);              // v22 §team-ch-key: preserve the team channel keypair across create/join (it is UNRECOVERABLE if dropped — no seed derives it)
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
    PhyArgs pa{};   // freq MHz / bw kHz (FRACTIONAL — 62.5 / 41.67 / 31.25 are valid LoRa BWs) / sf / layer — all four REQUIRED here
    char* p = buf; char* k; char* v;
    while (kv_next(p, k, v)) {
        if (phy_arg_take(pa, k, v, /*allow_layer=*/true)) continue;
        out.print(F("> join err bad/unknown key: ")); out.println(k); goto usage;
    }
    if (!(pa.has_freq && pa.has_bw && pa.has_sf && pa.has_layer) || !phy_args_in_range(pa, /*with_layer=*/true)) goto usage;
    {
        mrnv::Blob b{}; nv_load_stamped(b);   // §nv-ritual
        b.freq_mhz = pa.freq_mhz; b.bw_hz = meshroute::protocol::khz_to_hz(pa.bw_khz); b.routing_sf = (uint8_t)pa.sf;
        b.leaf_id = (uint8_t)(pa.layer & 0x0F); b.layer0_id = (uint8_t)pa.layer;   // full layer id stored; leaf = layer & 0x0F (byte-0 wire filter)
        b.node_id = 0; b.joined = 0; b.lineage_id = 0; b.config_epoch = 0;       // unprovisioned -> DAD + adopt the leaf's lineage via pull
        b.leaf_name_len = 0;                                                     // §clean-join: don't carry the OLD leaf's name into the new network — present as freshly-joined (config-not-yet-pulled). A managed leaf repopulates via the config pull; an unmanaged one shows blank until `cfg set leaf_name`. (Bytes need not be zeroed — len-gated.)
        if (!mrnv::save(b)) { out.println(F("> join err nv_save_failed")); return; }
        provision_apply_live(b, /*do_dad=*/true);
        meshroute::console::JoinStartedFields js{};   // JSON verb ack (replaces the human line): the app's start-of-DAD event
        js.layer = (uint8_t)pa.layer; js.leaf = (uint8_t)(pa.layer & 0x0F);
        js.freq_khz = meshroute::protocol::mhz_to_khz(pa.freq_mhz); js.sf = (uint8_t)pa.sf; js.bw_hz = b.bw_hz;
        const size_t m = meshroute::console::write_join_started(s_inbox_jb, sizeof s_inbox_jb, js);
        if (m) out.write(s_inbox_jb, m);
        return;
    }
usage:
    out.println(F("> join err usage: join layer=<1..255> freq=<MHz> bw=<kHz 7..500, fractional ok e.g. 62.5> sf=<5..12>   (leaf = layer & 0x0F)"));
}

// `create layer=<1..255> freq=<MHz> bw=<kHz> sf=<5..12> sf_list=<7,9> duty=<percent, 1 = 1%> name="<text>"
//         [active_fraction=<0..1>] [ch_min_ms=<ms>] [dm_min_ms=<ms>]` — join's floor + mint a MANAGED leaf (mother).
// The anti-spam keys are OPTIONAL: omitted => the protocol DEFAULTS (never inherited from the node's current settings).
void handle_create(const char* args, Print& out) {
    char buf[192]; size_t bn = 0; for (; args[bn] && bn < sizeof(buf) - 1; ++bn) buf[bn] = args[bn]; buf[bn] = '\0';
    PhyArgs pa{}; double dutypct = -1; uint16_t sfbm = 0;   // pa = the shared freq/bw/sf/layer floor (bw kHz, FRACTIONAL)
    char nm[meshroute::protocol::leaf_name_max]; uint8_t nlen = 0;
    float af = 0.125f; long chi = meshroute::protocol::channel_min_interval_ms, dmi = meshroute::protocol::dm_min_interval_ms;   // anti-spam DEFAULTS (overridden only if the key is given)
    bool hlist = false, hduty = false, hname = false;
    char* p = buf; char* k; char* v;
    while (kv_next(p, k, v)) {
        if (phy_arg_take(pa, k, v, /*allow_layer=*/true)) continue;   // create takes MORE keys than the shared floor — only freq/bw/sf/layer are shared
        if      (v && !strcmp(k, "sf_list"))         { sfbm = parse_sf_list(v); hlist = true; }
        else if (v && !strcmp(k, "duty"))            { dutypct = atof(v); hduty = true; }
        else if (v && !strcmp(k, "name"))            { for (const char* c = v; *c && nlen < sizeof(nm); ++c) nm[nlen++] = *c; hname = true; }
        else if (v && !strcmp(k, "active_fraction")) { af = (float)atof(v); }
        else if (v && !strcmp(k, "ch_min_ms"))       { chi = atol(v); }
        else if (v && !strcmp(k, "dm_min_ms"))       { dmi = atol(v); }
        else { out.print(F("> create err bad/unknown key: ")); out.println(k); goto usage; }
    }
    if (!(pa.has_freq && pa.has_bw && pa.has_sf && pa.has_layer && hlist && hduty && hname)) goto usage;
    if (!phy_args_in_range(pa, /*with_layer=*/true) || sfbm == 0 || dutypct < 0.0 || dutypct > 100.0) goto usage;
    if (af <= 0.0f) af = 0.125f; if (af > 1.0f) af = 1.0f;                    // clamp; 0/absent -> the default
    if (chi < 1) chi = meshroute::protocol::channel_min_interval_ms; if (dmi < 1) dmi = meshroute::protocol::dm_min_interval_ms;
    {
        mrnv::Blob b{}; nv_load_stamped(b);   // §nv-ritual
        b.freq_mhz = pa.freq_mhz; b.bw_hz = meshroute::protocol::khz_to_hz(pa.bw_khz); b.routing_sf = (uint8_t)pa.sf;
        b.leaf_id = (uint8_t)(pa.layer & 0x0F); b.layer0_id = (uint8_t)pa.layer;    // full layer id; leaf = layer & 0x0F
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
        meshroute::console::JoinStartedFields js{};   // JSON verb ack (replaces the human line): create adds create/lineage/leaf_name
        js.create = true;
        js.layer = (uint8_t)pa.layer; js.leaf = (uint8_t)(pa.layer & 0x0F);
        js.lineage = lin; js.leaf_name = nm; js.leaf_name_len = nlen;
        js.freq_khz = meshroute::protocol::mhz_to_khz(pa.freq_mhz); js.sf = (uint8_t)pa.sf; js.bw_hz = b.bw_hz;
        const size_t m = meshroute::console::write_join_started(s_inbox_jb, sizeof s_inbox_jb, js);
        if (m) out.write(s_inbox_jb, m);
        return;
    }
usage:
    out.println(F("> create err usage: create layer=<1..255> freq=<MHz> bw=<kHz 7..500, fractional ok e.g. 62.5> sf=<5..12> sf_list=<e.g.7,9> duty=<percent, 1 = 1%, fractional ok e.g. 0.1 = 0.1%> name=\"<text>\" [active_fraction=<0..1>] [ch_min_ms=<ms>] [dm_min_ms=<ms>]   (leaf = layer & 0x0F)"));
}

#if MR_FEAT_MOBILE
// §mobile 6.4 / §3-A.7: THE `[freq=<MHz> sf=<5-12> bw=<kHz>]` PHY tail, shared by `team new|<id>` and
// `mobile register`. Both verbs set the CURRENT layer's PHY from an operator-typed triplet, and both used to spell
// the whole ritual out — tokenize, reject unknown keys, require freq=, domain-check, build a LayerConfig.
// `layer=` is deliberately NOT accepted (neither verb ever did — see phy_arg_take's allow_layer).
//
// Returns `none` when the tail holds no tokens at all: `team 0` / `team <id>` with no PHY must skip the block
// silently, which is NOT the same as the "you gave args but no freq=" error. The per-verb strings are parameters
// because the console text is user-visible and differs verb by verb — including `team`'s own inconsistency
// (`> team err bad/unknown key:` vs `> team new err:`), which is preserved rather than tidied (C1).
// `bw_khz` comes back raw for the caller's echo line: re-deriving it from phy.bw_hz would round a second time.
enum class PhyTail : uint8_t { none, ok, error };
struct PhyTailMsgs {
    const __FlashStringHelper* bad_key;    // printed, then the offending key
    const __FlashStringHelper* need_freq;  // tokens present but no freq=
    const __FlashStringHelper* range;      // freq/sf/bw outside the shared domain
};
static PhyTail parse_phy_tail(const char* tail, uint8_t layer_id, const PhyTailMsgs& msg, Print& out,
                              meshroute::LayerConfig& phy, double& bw_khz) {
    char pb[96]; size_t pn = 0; for (const char* q = tail; *q && pn < sizeof(pb) - 1; ++q) pb[pn++] = *q; pb[pn] = '\0';
    char* pp = pb; char* k; char* v;
    PhyArgs pa{}; pa.bw_khz = 125.0;   // sf is REQUIRED with freq (0 fails the 5..12 check); bw is OPTIONAL, default 125 kHz — both as before
    bool any = false;
    while (kv_next(pp, k, v)) {
        any = true;
        if (phy_arg_take(pa, k, v, /*allow_layer=*/false)) continue;
        out.print(msg.bad_key); out.println(k); return PhyTail::error;
    }
    if (!any) return PhyTail::none;
    if (!pa.has_freq) { out.println(msg.need_freq); return PhyTail::error; }
    if (!phy_args_in_range(pa, /*with_layer=*/false)) { out.println(msg.range); return PhyTail::error; }
    phy = meshroute::LayerConfig{};
    phy.layer_id = layer_id; phy.routing_sf = (uint8_t)pa.sf; phy.freq_mhz = pa.freq_mhz;
    phy.bw_hz = meshroute::protocol::khz_to_hz(pa.bw_khz); phy.allowed_sf_bitmap = (uint16_t)(1u << pa.sf);
    bw_khz = pa.bw_khz;
    return PhyTail::ok;
}
#endif   // MR_FEAT_MOBILE (parse_phy_tail)

#if MR_FEAT_TEAM
// §team-ch-key (T-K1): the REPORTING half of mrfw::split_team_key_tail (firmware_config_parse.h). The parsing
// itself lives in that pure header so the native suite can reach it — no scenario runs a console verb, so logic
// left here would have zero automated coverage. This wrapper owns only the two scratch buffers and the strings.
// C2: every failure mode REFUSES loudly; half a keypair, or a truncated hex blob silently accepted, would install
// a key the operator never typed and then encrypt for a team that cannot read it.
static mrfw::TeamKeyTail parse_team_key_tail(const char* tail, char* rest, size_t rest_cap,
                                             uint8_t pub[32], uint8_t priv[32], Print& out) {
    // STATIC, not stack: handle_team's frame already carries a ~272 B mrnv::Blob, and a tail holding two 64-digit
    // hex blobs needs ~180 B more (the do_post_ack stack-overflow lesson). Console dispatch is single-threaded,
    // one command at a time, so there is no reentrancy concern.
    static char scratch[224];
    const char* bad_key = nullptr;
    const mrfw::TeamKeyTail r = mrfw::split_team_key_tail(tail, scratch, sizeof scratch, rest, rest_cap,
                                                          pub, priv, bad_key);
    switch (r) {
        case mrfw::TeamKeyTail::none:
        case mrfw::TeamKeyTail::ok:
            break;
        case mrfw::TeamKeyTail::bad_hex:
            out.print(F("> team err: ")); out.print(bad_key ? bad_key : "tkpub/tkpriv");
            out.println(F(" needs EXACTLY 64 hex digits (32 bytes)"));
            break;
        case mrfw::TeamKeyTail::half_pair:
            out.println(F("> team err: tkpub= and tkpriv= must be given TOGETHER (a keypair, not a half)"));
            break;
        case mrfw::TeamKeyTail::too_long:
            out.println(F("> team err: args too long"));
            break;
    }
    return r;
}
#endif   // MR_FEAT_TEAM (parse_team_key_tail)

// §team-ch-key (T-K1b): `team exportkey` -> the contract's team_key_export event, or a LOUD refusal.
// ★ OWNER RULING 2026-07-29: available on EVERY transport (USB / BLE / companion). That is why there is NO transport
// check here — the exfiltration risk was put to the owner explicitly and "any transport" was chosen; it is recorded in
// ios-companion/INBOX_SYNC_CONTRACT.md's ACCEPTED-RISK block, whose stated consequence is that the open "BLE fallback
// exposes the full console" item is now the ONLY control protecting this key. Do not add a gate here without a ruling.
//
// ★★ THE KEYLESS ANSWER IS A REFUSAL, NOT A NULL-BEARING SUCCESS OBJECT. The contract left it ambiguous ("tkpub/tkpriv
// null — or a loud refusal"); this slice picks the refusal. Three reasons, in order of weight:
//   1. C2. A `team_key_export` envelope whose two key fields are null is a SUCCESS event reporting a FAILURE.
//   2. The consumer of this event is the "Share team" QR encoder. A null-blind encoder writes the literal `null` — or,
//      worse, 32 zero bytes — into a QR, and an all-zero scalar is precisely what team_channel_key_derive REFUSES
//      (identity.h) because it is a silent, fatal non-key. A distinct `ev` cannot be mistaken for a payload; a null
//      field inside a success object can.
//   3. It is this surface's established idiom: `mobile_err{reason}` / `peerkey_err{reason}` answer "this verb does not
//      apply to this node", and console_json.cpp emits ZERO JSON `null` literals — every optional field there is
//      OMIT-when-absent. A null here would invent a new encoding convention for exactly one event.
// The app never needs the null form: `team_ch_key` on `ready`/`cfg` is the presence indicator, so reaching this refusal
// means a bug or a race, and being told so is more useful than an empty success.
//
// No `#if MR_FEAT_TEAM` needed, and that is deliberate rather than an omission: NodeConfig::team_id is ungated
// (node_carriers.h:93) and the three team_channel_* accessors have `#else` stubs returning false/nullptr (node.h), so on
// a MR_FEAT_TEAM 0 build this function compiles unchanged and answers `no_key` BY CONSTRUCTION — one app-facing code
// path, no silent success. (In practice unreachable there: handle_team is `#if MR_N_LAYERS < 2` and MR_FEAT_TEAM 0
// arrives only with MR_PROFILE_GATEWAY, which sets MR_N_LAYERS=2.)
static void team_export_key(Print& out) {
    const meshroute::NodeConfig& c = g_node.config();
    if (c.team_id == 0) {
        // No team ⇒ the event's own team_id field would be 0, i.e. a QR that provisions `team 0` = LEAVE. Refuse rather
        // than emit an incoherent export. ⚠ This is NOT a ruling on T-K1's deferred question (set_team_id does not clear
        // the key, so `team new` then `team 0` leaves a key behind with team_id==0): the key is untouched here — this
        // path only declines to EXPORT one that has no team to belong to. T-K2 still owns the clear-on-switch decision.
        const size_t m = meshroute::console::write_team_key_err(s_inbox_jb, sizeof s_inbox_jb, "no_team");
        if (m) out.write(s_inbox_jb, m);
        return;
    }
    const uint8_t* pub  = g_node.team_channel_pub();
    const uint8_t* priv = g_node.team_channel_priv();
    if (!pub || !priv) {   // the accessors return nullptr while keyless (node.h) — never a zero buffer to mistake for a key
        const size_t m = meshroute::console::write_team_key_err(s_inbox_jb, sizeof s_inbox_jb, "no_key");
        if (m) out.write(s_inbox_jb, m);
        return;
    }
    // VERBATIM, no re-derivation. T-K1 stores the canonical RFC-7748 CLAMPED scalar (identity.h's clamping contract)
    // exactly so that no consumer re-derives or normalises: re-deriving tkpub from tkpriv here would be redundant AND a
    // second place for the two halves to disagree. Emit both as stored.
    const size_t m = meshroute::console::write_team_key_export(s_inbox_jb, sizeof s_inbox_jb, c.team_id, pub, priv);
    if (m) out.write(s_inbox_jb, m);
}

// §team-ch-key (T-K3): `team grantkey <0xhash|team-id> [name="…"] [-t]` — GRANT the team CONTENT keypair to a vetted
// teammate over a SEALED DATA_TYPE_TEAM_KEY_GRANT DM. The grammar lives in mrfw::parse_grant_args; the send lives in
// Node::team_key_grant_send; this function is the REPORTING half only (one place that turns an outcome into an event).
//
// ★ EVERY refusal is a DISTINCT `team_key_err` reason, reusing T-K1b's error event (U1) rather than inventing a second
// error shape for the same verb family — the app must be able to tell "ask a teammate for the key" from "scan the QR"
// from "you typed a stranger's hash". Success is its own event (write_team_key_grant), never a happy-path `err`.
//
// ★★ `no_pubkey` NAMES ITS REMEDY IN THE TEXT, and that is a live-bench requirement rather than politeness: EVERY
// send-by-hash locate in the codebase passes want_pubkey=false (node_hashlocate.cpp:1002/1101/1107), so an operator who
// has merely *resolved* a teammate still holds no pubkey for it and a sealed send fails with nothing to act on. This
// verb will hit that constantly. We deliberately do NOT auto-issue a WANT_PUBKEY locate to paper over it — that would
// silently prefer the on-air TOFU path over the MITM-resistant QR ceremony for the one operation that ships a PRIVATE
// key (the reasoning is repeated at the refusal itself in Node::team_key_grant_send).
static void team_grant_key(const char* tail, Print& out) {
    static char scratch[128];                          // STATIC, same reasoning as parse_team_key_tail's (this frame already carries a Blob-sized console stack)
    mrfw::GrantArgsOut ga; const char* bad = nullptr;
    const mrfw::GrantArgs r = mrfw::parse_grant_args(tail, scratch, sizeof scratch, ga, bad);
    switch (r) {
        case mrfw::GrantArgs::missing:
            out.println(F("> team err usage: `team grantkey <0xhash|team-id> [name=\"<text>\"] [-t]`"));
            out.println(F(">   0x… = the teammate's key_hash32 · a bare 1..254 = its team_local_id (implies -t)"));
            return;
        case mrfw::GrantArgs::bad_target:
            out.println(F("> team err: grantkey target must be `0x` + 1..8 hex digits (a key_hash32) or a decimal 1..254 (a team_local_id)"));
            return;
        case mrfw::GrantArgs::bad_key:
            out.print(F("> team err bad/unknown key: ")); out.println(bad ? bad : "?");
            return;
        case mrfw::GrantArgs::too_long:
            out.println(F("> team err: args too long (name= is capped at 32 characters)"));
            return;
        case mrfw::GrantArgs::ok:
            break;
    }
    uint32_t hash = ga.target_hash;
    if (hash == 0) {                                   // a bare team_local_id -> resolve via the beacon-only team key cache
        if (!g_node.team_key_of_id(ga.target_id, hash) || hash == 0) {
            const size_t m = meshroute::console::write_team_key_err(s_inbox_jb, sizeof s_inbox_jb, "bad_target");
            if (m) out.write(s_inbox_jb, m);
            out.print(F("> team err: team_local_id ")); out.print(ga.target_id);
            out.println(F(" has not been heard (no beacon) — its key_hash32 is unknown. Address it by 0x<hash>, or wait for a beacon."));
            return;
        }
    }
    uint16_t ctr = 0;
    const auto res = g_node.team_key_grant_send(hash, ga.name_len ? ga.name : nullptr, ga.name_len,
                                                ga.team_plane ? meshroute::Plane::TEAM : meshroute::Plane::AUTO, &ctr);
    const char* reason = nullptr;
    const __FlashStringHelper* detail = nullptr;
    switch (res) {
        case meshroute::Node::TeamKeyGrantTx::queued: {
            const size_t m = meshroute::console::write_team_key_grant(s_inbox_jb, sizeof s_inbox_jb, hash, ctr);
            if (m) out.write(s_inbox_jb, m);
            out.print(F("> team grantkey: SEALED grant to 0x"));
            { char hx[9]; snprintf(hx, sizeof hx, "%08lX", (unsigned long)hash); out.print(hx); }
            if (ctr) { out.print(F(" queued ctr=")); out.println(ctr); }
            else     out.println(F(" PARKED (resolving the target's node id — it flies when the binding arrives)"));
            return;
        }
        case meshroute::Node::TeamKeyGrantTx::no_team:
            reason = "no_team";  detail = F("> team err: not in a team — there is no team key to grant (`team new` or `team <id>` first)"); break;
        case meshroute::Node::TeamKeyGrantTx::no_key:
            reason = "no_key";   detail = F("> team err: this node holds NO team channel key — ask a teammate to grant you one, or scan the team QR"); break;
        case meshroute::Node::TeamKeyGrantTx::no_identity:
            reason = "no_identity"; detail = F("> team err: no E2E crypto identity, so the grant cannot be SEALED — and a grant is never sent in the clear"); break;
        case meshroute::Node::TeamKeyGrantTx::no_pubkey:
            reason = "no_pubkey";
            detail = F("> team err: no VERIFIED pubkey for that target, so the grant cannot be sealed to it. Run `reqpubkey <0xhash>` (or import its QR), then retry.");
            break;
        case meshroute::Node::TeamKeyGrantTx::self:
            reason = "self";     detail = F("> team err: that hash is THIS node — a grant to yourself is a mis-address, not a send"); break;
        case meshroute::Node::TeamKeyGrantTx::delegated:
            reason = "delegated";
            detail = F("> team err: a grant cannot be DELEGATED through your home (the sealed-relay wrapper cannot carry its type). Grant over the team plane (`-t`), or from a node on the target's own layer.");
            break;
        case meshroute::Node::TeamKeyGrantTx::too_large:
            reason = "too_large"; detail = F("> team err: name= too long for the grant body (max 32 characters)"); break;
    }
    const size_t m = meshroute::console::write_team_key_err(s_inbox_jb, sizeof s_inbox_jb, reason);
    if (m) out.write(s_inbox_jb, m);
    if (detail) out.println(detail);
}

// §mobile 6.1: FNV-1a over (key_hash32 ‖ nonce) = the 32-bit team_id (team_fnv1a32, firmware_config_parse.h).
// `team new` = MINT a fresh team_id = hash(our key ‖ HW-RNG nonce). `team <id>` = JOIN an existing team. `team 0` = leave.
void handle_team(const char* args, Print& out) {
    while (*args == ' ') ++args;
    const meshroute::NodeConfig& c = g_node.config();
    uint32_t t;
    const char* phy_args = nullptr;
    // §team-ch-key (T-K1b): the THIRD subcommand, beside `new` and `<id>` (T-K3's `grantkey` is the fourth).
    // ★ ANSWERED FIRST, BEFORE the numeric parse below — that is a safety requirement, not ordering taste. `strtoul`
    // consumes ZERO digits from a non-numeric tail and returns 0 (verified, not assumed), so ANY subcommand that fell
    // through to it would be read as `team 0` = LEAVE THE TEAM instead of running.
    // ★★ §team-target (BUG FIX 2026-07-30): the residual hole that ordering could NOT close — a NEAR-spelling
    // (`team exportky`, `team grantky`, `team nwe`) matches none of these strncmps and used to reach the numeric parse,
    // i.e. LEAVE THE TEAM. It is now refused by parse_team_target below (firmware_config_parse.h: a target must lead
    // with a digit, and a value of 0 must additionally be an unambiguous zero spelling — `team 0x` and `team 08` were
    // silent leaves too). This ordering is now defence-in-depth rather than the only guard; both remain deliberate.
    if (!strncmp(args, "exportkey", 9)) {
        const char* tail = args + 9; while (*tail == ' ') ++tail;
        if (*tail) { out.println(F("> team err: `team exportkey` takes no arguments")); return; }   // C2: never silently ignore a tail — and never let one reach the leave path above
        team_export_key(out);
        return;
    }
    // §team-ch-key (T-K3): the FOURTH subcommand. Matched BEFORE the numeric parse for the same safety reason as
    // `exportkey` (strtoul would read `grantkey …` as `team 0` = LEAVE). ⚠ `grantkey` must be tested before any prefix
    // of it could match something else — it shares no prefix with `new`/`exportkey`, so order among the three is free.
    if (!strncmp(args, "grantkey", 8)) { team_grant_key(args + 8, out); return; }
    const bool mint_form = !strncmp(args, "new", 3);
    if (mint_form) {
        uint32_t nonce = 0; g_hal.rand_bytes(reinterpret_cast<uint8_t*>(&nonce), 4);
        t = team_fnv1a32(g_node.key_hash32(), nonce);
        phy_args = args + 3;   // §mobile 6.4: `team new [freq=<MHz> sf=<5-12> bw=<kHz>]` — optional team PHY
    } else if (parse_team_target(args, t, phy_args)) {
        // §6.4: `team <id> [freq= sf= bw=]` — a JOIN can set the shared team PHY too (mirrors `team new`). phy_args =
        // strtoul's endp, i.e. whatever follows the digits. ★ §team-target: entry now REQUIRES a leading digit, so a
        // mistyped subcommand can no longer arrive here as `team 0` (= leave) — see parse_team_target for the measurement.
    } else if (args[0]) {
        // ★★ §team-target (BUG FIX 2026-07-30) — C2: a non-numeric, non-subcommand tail is REFUSED LOUD. It must never
        // reach the numeric parse (strtoul -> 0 -> LEAVE) and must never be a silent no-op either. Nothing has been
        // touched at this point: no NV load/save, no set_team_id, no key mint/adopt, no PHY retune.
        out.print(F("> team err: unknown subcommand `")); out.print(args); out.println(F("` — a team id must BEGIN WITH A DIGIT."));
        out.println(F(">   NOTHING changed — team_id, the team channel key and NV are all as they were. (Before this check a"));
        out.println(F(">   mistyped subcommand parsed as `team 0` and LEFT THE TEAM.)"));
        out.println(F(">   valid: `team new` | `team <id>` | `team 0` (leave) | `team exportkey` | `team grantkey <target>`"));
        out.println(F(">   run `team` with no argument for the full usage."));
        return;
    } else {
        out.println(F("> team err usage: `team new [freq= sf= bw=]` (mint) | `team <id> [freq= sf= bw=]` (join) | `team 0` (leave) | `team exportkey` | `team grantkey <0xhash|team-id> [name=\"…\"] [-t]`"));
        out.println(F(">   both forms also take `[tkpub=<64 hex> tkpriv=<64 hex>]` to ADOPT an existing team channel key (else `team new` mints one)"));
        out.println(F(">   `team exportkey` prints this team's channel keypair as JSON (for the app's team QR) — it discloses a PRIVATE key"));
        out.println(F(">   `team grantkey <target>` sends this team's channel key to a teammate in a SEALED DM (needs a verified pubkey for it)"));
        return;
    }
    // §team-ch-key (T-K1): peel `tkpub=`/`tkpriv=` off the tail FIRST — parse_phy_tail below refuses unknown keys.
    // Nothing is applied here; we only VALIDATE + stage, so a malformed key refuses before any state moves.
#if MR_FEAT_TEAM
    uint8_t tk_pub[32] = {}, tk_priv[32] = {};
    bool tk_supplied = false;
    if (phy_args && *phy_args) {
        static char tk_rest[96];                        // STATIC (see parse_team_key_tail): keeps the console frame small AND outlives this block for the PHY parse below
        const mrfw::TeamKeyTail r = parse_team_key_tail(phy_args, tk_rest, sizeof tk_rest, tk_pub, tk_priv, out);
        if (r != mrfw::TeamKeyTail::none && r != mrfw::TeamKeyTail::ok) return;   // reported; team_id, the key, and NV are ALL unchanged
        tk_supplied = (r == mrfw::TeamKeyTail::ok);
        if (tk_supplied && t == 0) {                    // `team 0 tkpub=…` is meaningless — leaving a team takes no key
            out.println(F("> team err: tkpub=/tkpriv= make no sense on `team 0` (leave)"));
            return;
        }
        phy_args = tk_rest;                             // the tail parse_phy_tail sees has the team-key tokens REMOVED (it may now be empty)
    }
#endif
    mrnv::Blob b{}; nv_load_stamped(b);   // §nv-ritual — the stamp this path once MISSED (see nv_load_stamped) is now structural
    b.team_id = t;
    // §mobile 6.4 Fix 6: set the team PHY so teammates hear each other (AND a member can later register with a compatible
    // static network). Mirror `mobile register freq=`. Omitted -> keep the current PHY. Requires is_mobile (a team is mobile).
#if MR_FEAT_MOBILE
    if (phy_args && *phy_args && c.is_mobile) {
        const PhyTailMsgs msg{ F("> team err bad/unknown key: "),
                               F("> team err: PHY args need freq= (freq=<MHz> sf=<5-12> [bw=<kHz>])"),
                               F("> team new err: freq 100..1000 MHz, sf 5..12, bw 7..500 kHz") };
        meshroute::LayerConfig phy{}; double bw = 0.0;
        const PhyTail r = parse_phy_tail(phy_args, c.leaf_id, msg, out, phy, bw);
        if (r == PhyTail::error) return;
        if (r == PhyTail::ok) {                                    // `none` (empty tail, e.g. `team 0`) = keep the current PHY
            g_node.mobile_register_phy(phy);                       // retune the radio (+ kick the FSM -> team-DAD via the no-host path)
            b.freq_mhz = phy.freq_mhz; b.routing_sf = phy.routing_sf; b.bw_hz = phy.bw_hz; b.allowed_sf_bitmap = phy.allowed_sf_bitmap;   // PERSIST the team PHY
            out.print(F("> team PHY: freq=")); out.print(phy.freq_mhz, 3); out.print(F(" sf=")); out.print(phy.routing_sf); out.print(F(" bw=")); out.print(bw, 2); out.println(F(" kHz"));
        }
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
    // §team-ch-key (T-K1, spec §2.1 + ★ OWNER RULING 2026-07-29 "when team is created — a dedicated pair of key has
    // to be created"): the CREATOR always ends up holding a team channel keypair. Two ways in, and NO opt-out:
    //   · `tkpub=`/`tkpriv=` supplied  -> ADOPT them verbatim (canonicalised + cross-checked). This is the T-K4 QR
    //     onboarding path, which a JOINER uses too — hence both `team new` and `team <id>` accept the pair (spec
    //     §2.4: "the app provisions its node over the existing companion channel (`team …` + the new key fields)").
    //   · `team new` with no pair      -> MINT from the HAL CSPRNG.
    //   · `team <id>` with no pair     -> generate NOTHING. A joiner is meant to RECEIVE the key (T-K3 grant / T-K4
    //     QR); minting an unrelated second key here would silently split the team's readership in two. It is also
    //     what keeps the simulator corpus draw-free — see the §sim-team-verb note in NodeRuntimeWrapper.cpp.
    // ★ Placed BEFORE set_team_id so a refusal leaves the team UNJOINED rather than keyless (C2 — the owner ruling
    // removed the opt-out precisely because a keyless creator is a footgun). The only state already applied at this
    // point is the optional PHY retune above, which is the pre-existing shape of the incomplete-PHY refusal too.
#if MR_FEAT_TEAM
    if (tk_supplied) {
        if (!g_node.team_channel_key_adopt(tk_pub, tk_priv)) {
            out.println(F("> team err: tkpub=/tkpriv= REFUSED — not a valid X25519 keypair (all-zero, or tkpub is not tkpriv's public key). Team NOT joined."));
            return;
        }
        out.println(F("> team channel key: ADOPTED (from tkpub=/tkpriv=)"));
    } else if (mint_form) {
        if (!g_node.team_channel_key_mint()) {
            out.println(F("> team err: team channel keygen FAILED (crypto RNG returned no entropy). Team NOT minted."));
            return;   // C2: refuse the whole verb — never leave a creator holding no content key
        }
        out.println(F("> team channel key: MINTED (X25519)"));
    }
#endif
    // §clean-team (2026-07-27): ONE core call does the whole switch — drop the OLD team's learned plane (_rt_team /
    // _team_peer / team liveness / the team KEY CACHE / team RREQ ledgers) and the stale team-DAD id, then adopt the new
    // team_id LIVE. Returns false on a same-team no-op (`team <current_id>`), which clears nothing and skips the re-DAD.
    const bool team_switched = g_node.set_team_id(t);
    if (c.is_mobile && t != 0 && team_switched) g_node.team_dad_fire();   // §6.4: bootstrap the team plane (self-assign a _team_local_id, no static host needed)
    b.node_id       = g_node.canonical_node_id();            // §6.4: team_dad_fire may have MOVED node_id (off-grid: node_id==team id) -> persist the live id, don't re-save the stale one loaded at entry
    b.team_local_id = g_node.team_local_id();                // §6.4: persist the fresh id (or 0 on leave) alongside team_id
    blob_take_team_channel_key(b);                            // §team-ch-key (v22): persist the pair we just minted/adopted (or carry the existing one through a leave)
    if (!mrnv::save(b)) out.println(F("> team err nv_save_failed (team is LIVE but NOT persisted — will revert on reboot)"));   // §3-A.4: was the ONLY unchecked save of 9 (the LIVE team state above is already applied, so report — don't roll back)
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
        } else if (*p) {
            const PhyTailMsgs msg{ F("> mobile register err bad/unknown key: "),
                                   F("> mobile register err: args need freq= (freq=<MHz> sf=<5-12> [bw=<kHz>] | scan | <none>)"),
                                   F("> mobile register err: freq 100..1000 MHz, sf 5..12, bw 7..500 kHz") };
            meshroute::LayerConfig phy{}; double bw = 0.0;
            const PhyTail r = parse_phy_tail(p, c.leaf_id, msg, out, phy, bw);
            if (r == PhyTail::error) return;
            if (r == PhyTail::none) { out.println(msg.need_freq); return; }   // unreachable (*p is non-space here, so kv_next yields >=1 token) — mirrors the old bare `if (!hfreq)`
            g_node.mobile_register_phy(phy);
            out.print(F("> mobile register: on freq=")); out.print(phy.freq_mhz, 3); out.print(F(" sf=")); out.print(phy.routing_sf); out.print(F(" bw=")); out.print(bw, 2); out.println(F(" kHz"));
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
        m.freq_khz = meshroute::protocol::mhz_to_khz(pf);   // MHz double -> integer kHz (rounded; no float on the wire)
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
    mrnv::Blob b{}; nv_load_stamped(b);   // §nv-ritual (only freq is kept below, but the read is the same one)
    const double keep_freq = b.freq_mhz;
    b = mrnv::Blob{};                                                        // zero everything...
    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;
    b.freq_mhz = keep_freq;                                                  // ...keep only freq
    b.bw_hz = meshroute::protocol::khz_to_hz(LORA_BW); b.routing_sf = LORA_SF; b.cr = LORA_CR; b.tx_power = LORA_TX_POWER;   // §bw-round-invariant
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
    mrnv::Blob b{}; nv_load_stamped(b);   // §nv-ritual
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
