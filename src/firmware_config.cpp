// MeshRoute — src/firmware_config.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The config/provisioning cluster (see firmware_config.h) moved VERBATIM from fw_main.cpp (cleanup 2026-07-14).
// Increment A: apply_radio_live + handle_cfg_set + gw_*_err_str/handle_gateway. Shared device state comes from
// fw_context.h; behaviour-preserving (only relocated — the fw_main `P::` alias becomes meshroute::protocol::).
#include "firmware_config.h"
#include "fw_context.h"              // g_radio, g_iradio, g_hal, g_node, g_identity, g_freq_mhz, g_tx_power, g_radio_ok, g_lat_e7/lon_e7, g_ble_*
#include "firmware_config_parse.h"   // mrfw::parse_sf_list
// ⛔ CORRECTED IN PLACE 2026-08-13 (§UI-14): this line was a §UI-13 COMPILE-COVERAGE ANCHOR — *"an include with no
// call yet … the service is HEADLESS … `ICfgStore`/`ICfgLive` have no device binding"* — and that is now FALSE. The
// bindings are IN THIS FILE ([[B193]], see `device_cfg_store` / `device_cfg_live` below), so the include is load-
// bearing and the header arrives through `firmware_config.h` regardless. The anchor's reasoning is kept because it
// still holds for every env that does NOT compile the OLED: this TU is the one every board build compiles, so the
// service's ABI is verified on arm-none-eabi and xtensa even where nothing constructs one.
#include "firmware_config_service.h"
// ★★ §PROV-TX ([[B207]], spec 2026-08-17): the typed team-provisioning TRANSACTION `handle_team` now runs on, plus the
//    one explicit-material `blob_put_team_channel_key` helper `blob_take_team_channel_key` below delegates to. Pure
//    (no Print, no Arduino, no globals) so the native suite reaches ALL of its decision logic — which is the point,
//    because THIS FILE is compiled by neither the native suite nor the simulator.
#include "firmware_provisioning_service.h"
// ★★ §UI-15 slice 1: the typed STATIC-JOIN transaction `handle_join` now runs on — a SIBLING of the team one, ⛔ not
//    an overload of it (`ProvErr` is the team vocabulary). Same reason for existing: pure, so the native suite reaches
//    its decision logic, which nothing compiling THIS file can.
#include "firmware_join_service.h"
#include "firmware_join_profiles.h"   // §UI-15 slice 2: mrfw::JoinProfileService — the /mrjoin preset store (pure; this file only binds + renders)
// ★ [[B211]]: `mrfw::print_sf_list` — THE ONE bitmap->CSV formatter (`firmware_commands.cpp:250`), reused by the
//   `> team PHY:` echo so no third implementation is written (U1). ⚠ The B211 brief stated this header was already
//   reachable here; it was NOT included by this TU (verified per V1) — one declaration-only include, no new seam.
#include "firmware_commands.h"
#include "mr_ui.h"                   // ★ §UI-14 follow-up: mr_ui_on_config_saved — the FEATURE-NEUTRAL notification
                                     //   seam (an inline no-op off the OLED profile, so no MR_FEAT_OLED appears here)
#include "node_role.h"               // ★ §role-model/B28: role_set_refusal — the O1/O2/R4 role-transition truth table (pure, natively tested)
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

// ================================================================ §notify-every-save — THE RULE, IN ONE PLACE
// ★★★ THE RULE A NEW `/mrcfg` WRITER MUST FOLLOW, and it is deliberately a RULE rather than a per-field judgement:
//     **every USER-INITIATED verb that persists `/mrcfg` calls `mr_ui_on_config_saved()` after a write that BOTH
//     HAPPENED AND SUCCEEDED.** ⛔ Never on a refusal, never before the write. Spec §3.6.1 requires an open OLED
//     draft to show `CFG! RELOAD` at the INSTANT the record moves under it, not at its next SAVE attempt.
// ★★ WHY THE RULE AND NOT "notify where a covered field provably changes": the second form makes every future writer
//    re-derive the table below, and a writer that forgets is SILENTLY non-compliant. The rule is safe because it is
//    SELF-LIMITING BY CONSTRUCTION — `mr_ui_on_config_saved` re-reads the record and hands it to
//    `ConfigService::note_external_write`, which compares ONLY the four covered fields (`ble_mode`, `e2e_dm`,
//    `intro_attach`, `mobile_autoregister`) against the draft's baseline ⇒ a save that moved nothing covered raises
//    NOTHING, and the repaint is edge-triggered so it cannot even ask for a redundant frame.
// ★ THE SEVEN USER-INITIATED SITES (all notify; the `file:line` a reader needs is the verb's own save):
//     handle_cfg_set · handle_gateway · handle_join · handle_create · handle_team · handle_leave · handle_password
//   ⓘ MEASURED, not assumed, and it is why `leave` was the blocker: `handle_leave` does `b = mrnv::Blob{}` and
//     restores only magic/version/freq/the radio defaults/beacon/duty/the three anti-spam knobs ⇒ it RESETS ALL FOUR
//     covered fields to 0 — the largest covered-field change any verb makes. `handle_gateway`'s load-FAILURE seed
//     writes `ble_mode` from the live global and leaves the other three at 0. The remaining four verbs assign none of
//     the four and carry the loaded record's values through unchanged (which is exactly the case the self-limiting
//     comparison above turns into a no-op).
// ⛔ THE INTERNAL WRITERS STAY SILENT, and the reason is measured rather than stylistic: `fw_main.cpp`'s ctr-lease /
//    join persist (it assigns node_id/claim_epoch/joined/channel_ctr/team_local_id), `fw_main.cpp`'s leaf-config
//    adopt (lineage/epoch/sf_bitmap/duty/anti-spam/leaf_name) and `firmware_remote.cpp`'s admin counter-floor and
//    pubkey-rotate writes (admin fields only) assign NONE of the four, and all four are load-IF-PRESENT or
//    load-or-seed so the covered bytes are carried through untouched. They are also not user-initiated and the lease
//    fires on a TIMER: notifying there would put a flash read on a periodic path for a latch that can never move.
// ⛔ NO `MR_FEAT_OLED` MAY APPEAR IN THIS FILE. `lib/hal/mr_ui.h` supplies the inline no-op off the OLED profile,
//    exactly as it does for `mr_ui_on_push`, so the config cluster never learns whether a panel exists.
//    (`tools/probe_board_ui/`'s W13 is the check that keeps it true; W12/W14-W19 pin the seven placements.)

// ================================================================== §UI-14 / [[B193]] — THE DEVICE BINDINGS
// ★★★ WHAT B193 RECORDED AS OWED, DISCHARGED HERE. §UI-13 shipped `ICfgStore`/`ICfgLive` with NO hardware
//     implementation and named the two obligations a binding inherits; these are those two, and nothing more.
//
// ★★ THE STORE. `load()` IS the §nv-ritual (`nv_load_stamped` above), NOT a bare `mrnv::load`. ⇒ IT CANNOT FAIL, and
//    that is the DESIGNED behaviour rather than an unchecked call: the ritual seeds from the LIVE config when nothing
//    is persisted (or the version is rejected) and stamps magic/version on both paths, so an unprovisioned chip OPENS
//    the editor on its live values instead of being refused. ⇒ `CfgOpen::no_record` and the `CfgSave::nv_failed` arm
//    that comes from a failed pre-write load are UNREACHABLE ON DEVICE, by construction. They remain reachable — and
//    tested — through a fake store, which is the only place a load failure can be produced at all.
// ⚠ `save()` is `mrnv::save`, which COALESCES a BYTE-IDENTICAL WHOLE RECORD (device_nv.h's H3 change detection: flash
//   wear plus the reset-during-write window). ⛔ CORRECTED IN PLACE 2026-08-13: this note used to claim the coalescing
//   meant *"a save of covered fields whose values did not move writes no flash EVEN IF a NON-covered field made the
//   blob differ"*. THAT IS BACKWARDS AND IS WITHDRAWN — the comparison is `memcmp` over the WHOLE `mrnv::Blob`, so a
//   non-covered difference makes the record differ and the write DOES happen. ★ The true half stands: for the
//   covered-fields case the two agree, because the service refuses a no-op save at its own gate 3 before the store is
//   ever asked — so an unchanged draft costs zero writes by the SERVICE's rule, not by the store's.
// ⛔⛔ AND THE LIMIT OF EVERY CLAIM MADE ABOUT THIS BINDING: nothing here is exercised by any automated gate. The
//    native suite and `tools/probe_firmware_ui/` both drive the service through FAKES (there is no NVS/LittleFS on the
//    host), and the board builds only COMPILE these functions. ⇒ NO flash write, NO wear and ⛔ NO reset-during-write
//    / power-cut behaviour (§3.6.5's "either the complete old record or the complete new record") is proved by any
//    green run. That half is a BENCH check (docs/2026-07-31-bench-test-script.md) and B193 keeps its 🧪 until it runs.
namespace {
struct DeviceCfgStore : ICfgStore {
    bool load(mrnv::Blob& out) override { nv_load_stamped(out); return true; }   // §nv-ritual — see above
    bool save(const mrnv::Blob& b) override { return mrnv::save(b); }            // ONE whole-record write
};
// ★★ THE EFFECTIVE SEAM — the RUNNING values, which legitimately differ from the persisted ones between a save and
//    the reboot. `ble_mode` is read from `g_ble_mode` and not from `NodeConfig`, because the BLE stack initialises
//    ONCE at boot from that global and nothing writes it at runtime — which is exactly what makes `reboot_required()`
//    derivable rather than latched (firmware_config_service.h says so, and `handle_cfg_set`'s `ble_mode` arm confirms
//    it: it writes the blob and sets `live = false`, never `g_ble_mode`).
struct DeviceCfgLive : ICfgLive {
    CfgValues effective() const override {
        const meshroute::NodeConfig& c = g_node.config();
        CfgValues v{};
        v.at(CfgField::ble_mode)            = g_ble_mode;
        v.at(CfgField::e2e_dm)              = c.e2e_dm ? 1 : 0;
        v.at(CfgField::intro_attach)        = c.intro_attach ? 1 : 0;
        v.at(CfgField::mobile_autoregister) = c.mobile_autoregister ? 1 : 0;
        return v;
    }
    // ★★★ THE OFF->ON BRIDGE IS THE SECOND HALF OF B193, AND IT IS THE ONE A FLAG-COPY LOSES. `mobile_autoregister`
    //     is the BOOT policy — `on_init` reads it once into `_mobile_home_desired` (§MH-S4b §4.2) — so a RUNTIME
    //     write needs an explicit verb, and in ONE direction only:
    //       · OFF -> ON  = an explicit request for home service ⇒ `mobile_register_current()`, the same verb the
    //         operator would have typed. Without it the flag is set and NOTHING discovers a home.
    //       · ON -> OFF  = ⛔ DELIBERATELY NOTHING: §4.2 keeps an already-started attachment session running
    //         independently of this initial-auto flag; only `mobile unregister` ends one.
    //     ⚠ IT IS THE SAME RULE AS `handle_cfg_set`'s `mobile_autoregister` arm, deliberately in the same file so the
    //     two cannot drift — and it is written out rather than shared because that arm also parses text, writes the
    //     pending blob and answers a `Print&`, none of which belongs on this seam.
    void apply_live(const CfgLiveFields& f) override {
        meshroute::NodeConfig& lc = g_node.mutable_config();
        lc.e2e_dm       = f.e2e_dm;
        lc.intro_attach = f.intro_attach;
        const bool was = lc.mobile_autoregister;
        lc.mobile_autoregister = f.mobile_autoregister;
#if MR_FEAT_MOBILE
        if (!was && f.mobile_autoregister && lc.is_mobile) g_node.mobile_register_current();
#else
        (void)was;   // ⚠ [[B169]]'s shape: on `gateway_heltec` (MR_FEAT_MOBILE 0) the test compiles out and the
                     //   capture is orphaned -> a board-only `-Wunused-but-set-variable`. Same fix, same reason, as
                     //   the `handle_cfg_set` arm ten lines of history above; warnings are gate-blocking.
#endif
    }
};
}  // namespace
// ⓘ Function-local statics: the OLED layer constructs its `ConfigService` over these at static-init time, and a
//   function-local static is initialised on first CALL — so there is no cross-TU initialisation-order question.
ICfgStore& device_cfg_store() { static DeviceCfgStore s; return s; }
ICfgLive&  device_cfg_live()  { static DeviceCfgLive  s; return s; }

// ★★ §role-model — THE ONE place a refused role transition is answered (spec §2.3 + owner rulings O1/O2 + R4).
// The DECISION is `meshroute::role_set_refusal` (node_role.h, pure + natively tested); this is only its voice, so the
// two verbs that can change the role — `cfg set mobile` and the team-IMPLIED promotion in handle_team — refuse
// through ONE message set instead of forking two (U1). Returns true iff refused, in which case the caller must
// return: nothing has been mutated yet at either call site.
// ★ Every message names the WAY OUT, because the whole reason these are refusals rather than cascades is that the
// cascade would destroy state the operator never mentioned (O1: the team channel key is UNRECOVERABLE — no seed
// derives it — plus the team-DAD id and the team routes; O2: the guests' home and reverse-ack path). `prefix` keeps
// each verb's established error grammar (`> cfg err …` / `> team err …`) so the companion's parser is unaffected.
static bool role_refused(meshroute::RoleSetRefusal r, const __FlashStringHelper* prefix, Print& out) {
    if (r == meshroute::RoleSetRefusal::none) return false;
    out.print(prefix);
    switch (r) {
        case meshroute::RoleSetRefusal::none: break;                             // unreachable (returned above); listed so -Wswitch stays exhaustive
        case meshroute::RoleSetRefusal::no_mobile_plane:
            out.println(F("role_refused no_mobile_plane — this firmware is built WITHOUT the mobile/team plane, so it cannot become a mobile. Flash a normal-node build."));
            break;
        case meshroute::RoleSetRefusal::gateway_static:
            out.println(F("role_refused gateway_is_static — a GATEWAY is two-layer infrastructure and carries the static plane for others, so it can never be reached THROUGH someone else. Set `cfg set n_layers 1` + reboot first."));
            break;
        case meshroute::RoleSetRefusal::hosting_mobiles:
            out.print(F("role_refused hosting_mobiles n=")); out.print(g_node.mobile_reg_count());
            out.println(F(" — becoming a mobile drops the STATIC plane this node carries, so its registered mobiles would lose their home and reverse-ack path with NO notification. Nothing changed."));
            break;
        case meshroute::RoleSetRefusal::in_a_team:
            out.println(F("role_refused in_a_team — a team member IS a mobile, so this node cannot go static while it holds a team_id. Say `team 0` (leave the team, keep the role) or `leave` (wipe everything) FIRST."));
            out.println(F(">   deliberately NOT cascaded: clearing your team from here would destroy the team channel KEY (unrecoverable — no seed derives it), the team-DAD id and the team routes, from a command that never mentioned teams."));
            break;
    }
    return true;
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
    else if (!strcmp(key, "host_mobiles"))     { const bool want_host = (atoi(val) != 0 || !strcmp(val, "on"));   // §mobile 2a: accept/host mobiles? LIVE-only (default ON; reverts on reboot — a mobile itself never hosts)
                                           // ★ §B132: REFUSE turning it on for a node that can never be a home. A gateway
                                           // time-multiplexes one radio across two leaves, so it cannot meet a home's
                                           // CONTINUOUS obligations (last-mile delivery, presence, hash proxying, liveness);
                                           // a mobile never hosts at all. Accepting the write would have set a config byte
                                           // that every decision site then ignores — a knob that lies. Turning it OFF is
                                           // always allowed (it is already the effective state). ⚠ REFUSED, and the core's
                                           // can_host_mobiles() still re-checks per site: this is the operator-facing half.
                                           if (want_host && (lc.is_gateway || lc.n_layers != 1 || lc.is_mobile)) {
                                               out.println(lc.is_mobile
                                                   ? F("> cfg err refused (host_mobiles: a MOBILE never hosts — it registers to a home, it is not one)")
                                                   : F("> cfg err refused (host_mobiles: a GATEWAY is never a mobile home — one radio split across two leaves cannot serve last-mile/presence/liveness continuously. Set `cfg set n_layers 1` + reboot first)"));
                                               return;
                                           }
                                           lc.host_mobiles = want_host; persist = false; }
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
    // --- ★★ `loc_in_dm` KEY REMOVED 2026-07-31 (§loc-per-send, open-bug-register B0). It set a persistent toggle that
    //     attached this node's coordinates to EVERY originated app DM on a size check alone, with no crypt gate, so a
    //     plaintext DM aired the position in the clear. Location is now requested PER MESSAGE with `send … -l` and the
    //     send is REFUSED if it would not be sealed. There is deliberately no replacement key: a per-send intent must
    //     not acquire a persistent home. `cfg set lat`/`lon` (-> /mrid) still hold the node's fix, which `-l` reads.
    //     An unknown key falls through to this handler's trailing bad_key branch, so an old script saying
    //     `cfg set loc_in_dm 1` now fails LOUD rather than silently doing nothing. ---
    // --- E2E §4b: originate app DMs ENCRYPTED. LIVE via mutable_config() + PERSISTED (NV v10). A no-pubkey CRYPTED send
    //     fails loud (send_failed{no_pubkey}); the user provisions keys via `peerkey`/`reqpubkey`. Default off = plaintext. ---
    else if (!strcmp(key, "e2e_dm"))     { b.e2e_dm = (atoi(val) != 0 || !strcmp(val, "on") || !strcmp(val, "true")) ? 1 : 0; lc.e2e_dm = (b.e2e_dm != 0); }
    // --- §chan-crypt CL2a (T-K2 §2.5): SEAL a `-t` team channel post by default when this node holds the team
    //     CONTENT key. Default ON; this is the OPT-OUT, and per open decision O2 it is the ONLY one (no per-send
    //     "air this in clear" flag — a footgun on a privacy feature). `send_channel -t -e` still seals explicitly
    //     with this off; what it turns off is only the DEFAULT.
    //     ★ LIVE-ONLY (`persist = false`), deliberately, and NOT the e2e_dm shape it sits beside — the
    //     team_hop_cap / nav / intra_layer_relay precedent: reboot reverts to the DESIGN value, and here the design
    //     value is the PRIVACY-SAFE one, so a forgotten opt-out heals itself instead of silently keeping a team
    //     channel in clear forever. It also keeps an unrequested NV kVersion bump (= another reprovision-on-reflash)
    //     out of this slice. ✖ MISSING, and deliberately: no NV field. If the owner wants the opt-out to survive a
    //     reboot, that is a device_nv v24 + fw_main boot-restore slice of its own.
    else if (!strcmp(key, "team_channel_crypt")) { lc.team_channel_crypt = (atoi(val) != 0 || !strcmp(val, "on") || !strcmp(val, "true")); persist = false; }
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
    // ★★ §role-model (2026-07-31): KEPT as the role key (owner ruling O3 — *"ok so we keep it — but we make it
    //    consistent"*), and it is now a RULED transition instead of a raw flag flip. Three refusals, all from the ONE
    //    truth table in node_role.h: O1 refuses a DEMOTION while `team_id != 0` (the B28 invariant — and NOT by
    //    cascading the team away); O2 refuses a PROMOTION while this node HOSTS mobiles (they would silently lose
    //    their home); R4 refuses a promotion on a GATEWAY (two-layer infrastructure is static, exclusively — this was
    //    entirely unruled before: the only combined `is_gateway && is_mobile` read in the tree is incidental).
    //    Re-asserting the role you already hold is not a transition and is never refused.
    // ⓘ LIVENESS, MEASURED NOT INHERITED — ★ the spec's §1.2 row for this key ("⚠ reboot-to-apply") and the register's
    //    third-enforcement-point bullet ("a raw flag flip … and is reboot-to-apply") are BOTH WRONG about the flag, and
    //    this handler is the evidence: `lc` IS `g_node.mutable_config()`, so the write below lands in the LIVE `_cfg`
    //    that all ~216 `is_mobile` readers consult, `live` is left at its `true` default (unlike `duty`/`ble_*`/`n_layers`
    //    /the `l1_*` block, which set it false explicitly), and the reply this path prints is `ok (live + saved)`, not
    //    `ok (reboot to apply)`. The section header two blocks up says the same: *"role/topology: LIVE via
    //    mutable_config() + PERSISTED"*. ⇒ the FLAG has always applied immediately, and because `is_mobile` is packed
    //    off `_cfg` at frame-build time (beacon bit 0x20 / J bit 0x40) the role is RE-ANNOUNCED on the very next beacon
    //    with no explicit re-beacon anywhere. There is therefore no reboot-to-apply asymmetry between this key and the
    //    team-implied promotion (spec §1.2 I-d dissolves), and R5's announcement requirement is already met on both.
    // ✖ MISSING, deliberately (spec §3.2, its own slice — C1): the STATE half of the transition. A demotion does NOT
    //    clear `_my_mobile_reg` (the home registration) or pending presence/registration state, and a promotion does
    //    not kick the registration FSM. That — not the flag, and not the announcement — is what "reboot to make it
    //    stick" was really about, and it is a behaviour change with its own gate. This slice is the INVARIANT + the
    //    refusals only.
    else if (!strcmp(key, "mobile"))       { const bool want_mobile = (atoi(val) != 0 || !strcmp(val, "true"));
                                             if (role_refused(meshroute::role_set_refusal(want_mobile, lc.is_mobile, lc.is_gateway, lc.team_id, g_node.mobile_reg_count()),
                                                              F("> cfg err "), out)) return;   // nothing mutated yet — b/lc untouched, no NV write
                                             lc.is_mobile    = want_mobile;                            b.is_mobile    = lc.is_mobile    ? 1 : 0; }
    // --- ★★ `team_id` KEY REMOVED 2026-07-31 (§team-id-cfg-removal, open-bug-register B27). It was a FORKED, UNGUARDED
    //     DUPLICATE of a destructive operation, not a feature: `set_team_id((uint32_t)strtoul(val, nullptr, 0))` DISCARDED
    //     the endptr, so unlike mrfw::parse_team_target (firmware_config_parse.h) it enforced no leading-digit rule, no
    //     whole-token consumption and no 32-bit range — all three defects live on a LIVE team switch reachable from every
    //     transport: `cfg set team_id exportky` -> strtoul 0 -> LEFT THE TEAM · `88A672BA` -> joined team 88 ·
    //     `4294967296` -> LEFT on a 64-bit host / joined garbage 0xFFFFFFFF on the 32-bit boards.
    //     ⇒ REMOVED, not guarded (owner ruling): `team new` / `team <id>` / `team 0` (handle_team below) already do
    //     everything this key did PLUS those three guards, the PHY tail, channel-key minting, team_dad_fire and the full NV
    //     persist. There is deliberately NO replacement key and none should be re-added — a second spelling of a
    //     destructive switch is the exact fork U1 exists to prevent. An unknown key falls through to this handler's trailing
    //     unknown_key branch (below), so `cfg set team_id 5` now fails LOUD instead of silently switching teams. §clean-team
    //     still holds and is now STRONGER: Node::set_team_id() is the one core entry point for every live team change, and
    //     handle_team is its ONLY src/ caller.
    //     ★★ EVERY READ SURFACE STAYS — only the WRITE is gone: the `cfg`/`status` text dumps (firmware_commands.cpp),
    //     the JSON (console_json.h), and ⚠ the binary TLV TAG_CFG_TEAM_ID = 0x12, which is **NOT** retired the way
    //     0x18/ex-`loc_dm` above was: dec_cfg is a pure decoder called from no src/ file, so that tag is a read-OUT the
    //     companion depends on ("team_id ALWAYS present in cfg"). Retiring it would break the app.
    //     ✔ The `team_id != 0 && !is_mobile` role hole this marker used to leave OPEN is now CLOSED (§role-model, B28/R2,
    //     2026-07-31): `Node::set_team_id` promotes to mobile on a non-zero team, fw_main's NV restore normalises the same
    //     implication at boot (NV still persists the two fields independently — that path is the backstop, not a leak),
    //     and `cfg set mobile` below refuses the transitions that would break it. ---
    // ★★★ §MH-S4b §4.2 — `mobile_autoregister` IS THE BOOT POLICY, read once at `on_init` into the live session state
    // `_mobile_home_desired`. So a runtime write of this key needs an explicit bridge, and only in ONE direction:
    //   · OFF -> ON  = an explicit request for home service ⇒ route it through `mobile_register_current()`, which is
    //     the same verb the operator would have typed. ⚠ Before this it armed the OLD OR-ed predicate and scheduled
    //     NOTHING, so the mobile did not actually DISCOVER until some other event happened to arm a timer.
    //   · ON -> OFF  = ⛔ DELIBERATELY NOTHING. §4.2: "once an attachment session was explicitly started, …
    //     continue INDEPENDENTLY of this initial-auto flag." Only `mobile unregister` ends a live session; the flag
    //     change takes effect at the next boot, which is exactly what "boot policy" means.
    else if (!strcmp(key, "mobile_autoregister")) { const bool was = lc.mobile_autoregister;
                                                   lc.mobile_autoregister = (atoi(val)!=0 || !strcmp(val,"true")); b.mobile_autoregister = lc.mobile_autoregister?1:0;   // §mobile console: autonomy toggle (LIVE + persist)
#if MR_FEAT_MOBILE
                                                   if (!was && lc.mobile_autoregister && lc.is_mobile) g_node.mobile_register_current();
#else
                                                   (void)was;   // ⚠ MR_FEAT_MOBILE 0 (gateway): the transition test compiles out, so the capture is unused — MEASURED as a NEW `-Wunused-but-set-variable` on `gateway_heltec` (census 175 vs the pinned 174) before this line existed. Warnings are gate-blocking.
#endif
                                                 }
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
    // ★★★ §3.6.1's IMMEDIATE NOTIFICATION (§UI-14 follow-up). Serial and BLE keep their immediate-write path — the
    // spec REQUIRES that for companion compatibility — so the OLED's draft has to be told the record moved, at the
    // moment it moves. ⛔ Without this the panel learns of a conflict only at its next SAVE attempt, and the
    // `change → external REVERT → SAVE` case is not caught there at all: the bytes match again, the latch was never
    // raised, and the save proceeds over a companion change the operator never saw.
    // ★ THE PLACEMENT IS THE CONTRACT: AFTER the write, and only on the path where it BOTH happened and SUCCEEDED.
    //   The line above has already returned on failure, so reaching here with `persist` true means the record is
    //   durably written; `persist == false` means a live-only key (`nav`, `team_hop_cap`, `team_channel_crypt`, …)
    //   that has no durable representation to disagree with.
    // ⛔ NO `MR_FEAT_OLED` HERE, deliberately: `mr_ui.h` supplies an inline no-op off the OLED profile, exactly as it
    //   does for `mr_ui_on_push`, so this config path never learns whether a panel exists.
    // ⛔ CORRECTED IN PLACE 2026-08-13 ([[B194]]): this block used to end *"NOT WIRED … the OTHER `/mrcfg` writers —
    //   the provisioning verbs (`join`/`create`/`team`/`leave`) and fw_main's channel-ctr lease — do NOT notify"*.
    //   THAT IS NOW FALSE FOR THE SIX USER-INITIATED VERBS and is withdrawn: every one of them notifies, under the
    //   §notify-every-save rule stated once at the top of this file (which also records, measured, why the INTERNAL
    //   writers stay silent). ⚠ The withdrawn text's OTHER claim still stands and is kept: none of those verbs could
    //   produce last-writer-wins, because the service's SAVE-time gate 2b re-reads and refuses with zero writes —
    //   what they lacked was the IMMEDIATE marker.
    if (persist) mr_ui_on_config_saved();   // §notify-every-save — site 1 of 7
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
    mr_ui_on_config_saved();   // §notify-every-save — site 2 of 7. ⓘ Reachable on an OLED build: `gateway_heltec` is heltec_v3 (MR_FEAT_OLED=1) + MR_N_LAYERS=2. On the load-FAILURE path above the seed writes `ble_mode` live and leaves the other three covered fields at 0; on the load-SUCCESS path all four are carried through and the comparison is a no-op.

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

// §team-ch-key (T-K1): the LIVE-READING half of the one node->Blob conversion for the team channel keypair (U2 —
// never rebuild a carrier field-by-field per site). It COLLECTS the material from the node and DELEGATES the write to
// `mrfw::blob_put_team_channel_key` (src/firmware_provisioning_service.h).
// ★★ OWNER-RULED 2026-08-17 (§PROV-TX v4 §4): ONE EXPLICIT-MATERIAL HELPER, with this function delegating to it —
//    ⛔ NOT an overload pair, because an overload pair is exactly how the S1/L9 field-drop rot starts. §PROV-TX's
//    candidate composition calls THE SAME helper with STAGED material, so there is one conversion authority.
// Its callers now: seed_blob_from_live (the load-FAILED path, so a create/join reprovision on a fresh chip does not
// silently drop the key) and handle_leave/handle_cfg_set through that seed. ⓘ `handle_team` no longer calls it — the
// transaction composes its candidate from the STAGED pair (which is the whole point: what is persisted is what will
// be installed), so a live read there would have re-introduced the mutate-then-derive-the-candidate defect [[B207]].
// NB it reads through the ACCESSORS, which return nullptr while keyless — so there is no path where an
// unflagged buffer leaks into NV as if it were a key.
static void blob_take_team_channel_key(mrnv::Blob& b) {
    blob_put_team_channel_key(b, g_node.team_channel_pub(), g_node.team_channel_priv());
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
    b.e2e_dm     = nc.e2e_dm ? 1 : 0;   // §loc-per-send: `b.loc_in_dm` GONE with the NV field (kVersion 23) — location is per-send (`send -l`)
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

#if MR_N_LAYERS < 2   // §config-integrity: create/join are normal-node-only — compiled out on the gateway build (refused at dispatch)
// ================================================================ §UI-15 slice 1 — THE STATIC-JOIN DEVICE BINDING
// ★★ THE ONE THING THE JOIN TRANSACTION NEEDS FROM THE DEVICE, and it is a thin adapter for the same reason
//    §PROV-TX's three are: a decision taken here would be unreachable by every automated gate (this TU is compiled by
//    neither the native suite nor the simulator), which is why the logic lives in `src/firmware_join_service.h`.
// ⓘ The DURABLE seam is NOT re-bound: `device_cfg_store()` is reused unchanged (U1) — one `/mrcfg` record, one
//   whole-record write, and the same "false = THE WRITE FAILED (nothing may be applied live)" contract. Its `load()`
//   IS the §nv-ritual, so it cannot fail on device ⇒ `JoinErr::nv_load_failed` is UNREACHABLE ON DEVICE by
//   construction and is reachable — and tested — only through a fake store. That is stated, not glossed.
namespace {
// ⛔ ONE CALL AND NOTHING ELSE. The seam is *"apply the just-saved record live and start DAD"*; the §notify-every-save
//    hook deliberately does NOT live in here but at the VERB, beside its verdict guard, exactly as `handle_team`'s
//    does — see the note at `handle_join`'s notification for why the resulting ordering is unobservable.
struct DeviceJoinLive : mrfw::IJoinLive {
    void apply_and_start(const mrnv::Blob& b) override { provision_apply_live(b, /*do_dad=*/true); }
};
}  // namespace
// Function-local statics: constructed on first CALL, so there is no cross-TU initialisation-order question (the same
// reasoning as `device_cfg_store()` / `prov_service()`). ONE service instance, which §UI-15 slice 6's OLED adapter
// will reach for as well.
static mrfw::JoinService& join_service() {
    static DeviceJoinLive live;
    static mrfw::JoinService s(device_cfg_store(), live);
    return s;
}

// §UI-15 slice 1 — THE VOICE OF A NON-`started` VERDICT. Separate for the same two reasons `team_report_not_applied`
// is (U3: the verb stays parse -> request -> render; and ONE guarded early return is what lets a probe pin *"notify
// ONLY on the started arm"* as a compact source fact).
// ★★ IT RETURNS A BOOL, AND THAT IS THE POINT RATHER THAN A SHORTCUT: the four DOMAIN refusals speak with the ONE
//    shared usage line, which lives at `handle_join`'s `usage:` label. Re-spelling that string here would put a
//    SECOND copy of it in flash and create exactly the drift U1 exists to prevent. ⇒ `false` = "say nothing; the
//    caller's usage line is the answer", `true` = "the outcome has been reported".
// `-Wswitch` is `-Werror=switch` here, so both switches are `default`-less: a new verdict or a new `JoinErr` fails
// the build until its text is written.
static bool join_report_not_started(const mrfw::JoinResult& res, Print& out) {
    switch (res.verdict) {
        case mrfw::JoinVerdict::refused:
            switch (res.err) {
                // ⛔ ALL FOUR FALL TO THE SHARED USAGE LINE, AND THAT IS THE PRE-SLICE BEHAVIOUR PRESERVED VERBATIM
                //    (C1), not a design choice made here: `handle_join` has always answered every domain refusal
                //    with one usage line. The ARMS are distinct so §UI-15 slice 6's screen can say WHICH field is
                //    wrong — a usage line cannot — but ⛔ this slice changes not one console byte.
                case mrfw::JoinErr::invalid_layer:
                case mrfw::JoinErr::invalid_freq:
                case mrfw::JoinErr::invalid_bw:
                case mrfw::JoinErr::invalid_sf:
                    return false;
                // ⓘ UNREACHABLE ON DEVICE (see the binding note above: the store's `load()` is the §nv-ritual and
                //   returns true unconditionally). It is written anyway because the ARM exists and `-Werror=switch`
                //   requires it, and it speaks in the same voice as its `nv_save_failed` sibling.
                case mrfw::JoinErr::nv_load_failed:
                    out.println(F("> join err nv_load_failed"));
                    return true;
                case mrfw::JoinErr::nv_save_failed:   // carried by the nv_failed VERDICT below, never by this arm
                case mrfw::JoinErr::none:             // unreachable for a refusal; listed so -Wswitch stays exhaustive
                    return true;
            }
            return true;
        // ★ THE SAME LINE THE VERB HAS ALWAYS PRINTED, byte for byte — and now it means strictly more: the write was
        //   the commit point, so nothing was applied live and no DAD airtime was spent.
        case mrfw::JoinVerdict::nv_failed:
            out.println(F("> join err nv_save_failed"));
            return true;
        case mrfw::JoinVerdict::started:              // the caller guards on this; listed for exhaustiveness
            return true;
    }
    return true;
}

// `join layer=<1..255> freq=<MHz> bw=<kHz> sf=<5..12>` — set the radio floor + (re-)DAD; auto-pulls the leaf config (R6.2).
// ★ §UI-15 slice 1: parse -> request -> render. The decision logic (validate, load, compose ONE candidate, ONE save,
//   then the live apply) is `mrfw::JoinService::apply_join`; ⛔ nothing below re-derives any of it.
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
        // ⛔ THE NARROWING TO `uint8_t` IS SAFE ONLY BECAUSE `phy_args_in_range(pa, with_layer=true)` RAN ABOVE:
        //    `PhyArgs::layer` is a `long`, so 257 would otherwise narrow to a perfectly valid 1 and join the WRONG
        //    layer. ⓘ `bw` is handed over as the RAW operator kHz, ⛔ NOT pre-converted — see `JoinRequest::bw_khz`
        //    for the measured reason (a `uint32_t` Hz field changes what `bw=nan` does).
        mrfw::JoinRequest rq{};
        rq.layer = (uint8_t)pa.layer; rq.freq_mhz = pa.freq_mhz;
        rq.bw_khz = pa.bw_khz; rq.routing_sf = (uint8_t)pa.sf;
        const mrfw::JoinResult res = join_service().apply_join(rq);
        if (res.verdict != mrfw::JoinVerdict::started) { if (join_report_not_started(res, out)) return; goto usage; }
        mr_ui_on_config_saved();   // §notify-every-save — site 3 of 7 (assigns none of the four covered fields; the rule is the point, not the field list)
        // ⓘ ORDERING NOTE, because this call USED to sit between the save and `provision_apply_live` and now sits
        //   after both (the transaction owns the live apply, exactly as `handle_team`'s does). ★ MEASURED
        //   UNOBSERVABLE, not assumed: the hook only re-reads `/mrcfg` and compares the FOUR covered fields
        //   (`ble_mode`/`e2e_dm`/`intro_attach`/`mobile_autoregister`) against the OLED draft's baseline, and
        //   `provision_apply_live` writes NONE of the four — not in NV (it makes no NV write at all) and not in
        //   `NodeConfig`. So even on the §nv-ritual's seed-from-live path the blob it compares is identical either
        //   way, and neither call can print.
        meshroute::console::JoinStartedFields js{};   // JSON verb ack (replaces the human line): the app's start-of-DAD event
        js.layer = res.layer; js.leaf = res.leaf;     // ★ read off WHAT WAS PERSISTED (full byte / nibble), ⛔ not re-derived here
        js.freq_khz = meshroute::protocol::mhz_to_khz(pa.freq_mhz); js.sf = (uint8_t)pa.sf; js.bw_hz = res.bw_hz;
        const size_t m = meshroute::console::write_join_started(s_inbox_jb, sizeof s_inbox_jb, js);
        if (m) out.write(s_inbox_jb, m);
        return;
    }
usage:
    out.println(F("> join err usage: join layer=<1..255> freq=<MHz> bw=<kHz 7..500, fractional ok e.g. 62.5> sf=<5..12>   (leaf = layer & 0x0F)"));
}


// ================================================================ §UI-15 slice 2 — THE /mrjoin PROFILE-STORE BINDING
// ★★ THIN ON PURPOSE, for the third time in this arc and for the same measured reason: this TU is compiled by
//    NEITHER the native suite (`test_build_src = no`) NOR the simulator, and no corpus scenario runs a console verb.
//    ⇒ every DECISION lives in `src/firmware_join_profiles.h`, where `test/test_firmware_join_profiles.cpp` can COUNT
//    the writes; what is left here is parse -> call -> render.
// ⛔ NOT `device_cfg_store()`: this is a DIFFERENT record with a THREE-valued read (absent vs corrupt vs ok), which is
//    the whole point of §3.6.3's isolation. `/mrcfg` is not touched by any verb below.
namespace {
struct DeviceJoinProfileStore : mrfw::IJoinStore {
    mrnv::JoinRead load(mrnv::JoinBlob& out) override { return mrnv::load_join(out); }
    bool save(const mrnv::JoinBlob& b) override { return mrnv::save_join(b); }
};
}  // namespace
// Function-local statics: constructed on first CALL, so there is no cross-TU initialisation-order question (the same
// reasoning as `device_cfg_store()` / `join_service()`). ONE instance, which §UI-15 slice 6's OLED will reach for too.
static mrfw::JoinProfileService& join_profile_service() {
    static DeviceJoinProfileStore st;
    static mrfw::JoinProfileService s(st);
    return s;
}

// Which refusals are the operator MIS-TYPING something (⇒ show the grammar) and which are a STATE he must act on
// (⇒ the message already says what to do, and a usage line would bury it). `default`-less, so `-Werror=switch` fails
// the build when a `ProfileErr` arm is added without deciding which of the two it is.
static bool joinprofile_refusal_needs_usage(mrfw::ProfileErr e) {
    switch (e) {
        case mrfw::ProfileErr::bad_index:
        case mrfw::ProfileErr::invalid_layer:
        case mrfw::ProfileErr::invalid_freq:
        case mrfw::ProfileErr::invalid_bw:
        case mrfw::ProfileErr::invalid_sf:
        case mrfw::ProfileErr::not_finite:
        case mrfw::ProfileErr::name_too_long:
            return true;
        case mrfw::ProfileErr::none:
        case mrfw::ProfileErr::store_invalid:
        case mrfw::ProfileErr::store_io_failed:
        case mrfw::ProfileErr::needs_confirm:
        case mrfw::ProfileErr::nv_save_failed:
            return false;
    }
    return false;
}

// THE VOICE OF A PROBLEM. Returns `true` = "the outcome has been reported, stop"; `false` = "nothing is wrong, the
// caller says what happened" — the same bool contract `join_report_not_started` uses, for the same reason (ONE copy
// of the usage line, which lives at the verb's `usage:` label).
// ⓘ The four DOMAIN arms are UNREACHABLE FROM THIS VERB by construction — `phy_args_in_range` screens them at the
//   narrowing boundary below, exactly as `handle_join` does. They exist for §UI-15 slice 6's screen, which has no
//   `PhyArgs` in front of it and needs to say WHICH field is wrong. Stated, not glossed
//   ([[meshroute-mark-done-vs-missing-in-code]]).
static bool joinprofile_report_problem(const mrfw::ProfileResult& res, Print& out) {
    switch (res.verdict) {
        case mrfw::ProfileVerdict::ok:
        case mrfw::ProfileVerdict::unchanged:
        case mrfw::ProfileVerdict::empty:
            return false;
        // ⛔ THE LINE SAYS ONLY WHAT IS KNOWN — the ONE write attempt was reported as failed. It must ⛔ NOT go on
        //    to reassure the operator about the state of the flash: a backend can fail AFTER a partial physical
        //    write ([[B193]]), and no layer here can establish otherwise. §PROV-TX's S10 pins that prohibition over
        //    this whole file, prose included, which is why the forbidden sentence is not even quoted here.
        case mrfw::ProfileVerdict::nv_failed:
            out.println(F("> joinprofile err nv_save_failed"));
            return true;
        case mrfw::ProfileVerdict::refused:
            // ★★ THE HONESTY LINE. An unreadable store is NOT reported as an empty one, and the remedy is named
            //    because it is the ONLY one — and because it COSTS all four slots, which the operator must know
            //    before typing it.
            if (res.err == mrfw::ProfileErr::store_invalid) {
                out.println(F("> joinprofile err PROFILE STORE INVALID (unreadable; `joinprofile reset confirm` is the ONLY repair and it discards all 4 slots)"));
                return true;
            }
            // ★★★ THE SECOND HONESTY LINE, AND IT NAMES A DIFFERENT FAULT WITH A DIFFERENT REMEDY. The store could
            //     not be OPENED, so nothing whatever is known about the four profiles — ⛔ this must never read as
            //     `NO PROFILES` (the bug it was added for), and it must ⛔ not offer `reset confirm` either: that
            //     verb cannot repair a filesystem, and taking the suggestion would throw away four presets that may
            //     be perfectly intact behind a transient mount failure.
            if (res.err == mrfw::ProfileErr::store_io_failed) {
                out.println(F("> joinprofile err PROFILE STORE UNREADABLE - STORAGE FAILURE (the NV backend would not open; this is NOT an empty store - check `faults`, and treat it as a device fault, NOT as `joinprofile reset`)"));
                return true;
            }
            out.print(F("> joinprofile err ")); out.println(mrfw::profile_err_name(res.err));
            return !joinprofile_refusal_needs_usage(res.err);
    }
    return true;
}

// Take the next space-delimited word from *p (NUL-terminating it), or nullptr at end. ⛔ NOT `kv_next`: the verb and
// the slot index are POSITIONAL, not `key=value`, and feeding them to kv_next would report them as malformed keys.
// ★★ THE REFUSAL THE SERVICE NEVER SAW: the token was not an INDEX AT ALL (`2junk`, `1x`, ``). It is voiced through
//    the SAME reporter as `clear 0`, so a mistyped index and an out-of-range one read identically to the operator,
//    and the SERVICE stays the one authority for what a valid index IS (`valid_profile_slot`) — this only answers
//    "is this token a number", which is `mrfw::parse_index_strict`'s job and is native-tested there.
// ⛔ `ProfileResult{}` defaults to `refused`, which is the point: there is no path from here that writes.
static mrfw::ProfileResult joinprofile_bad_index() {
    mrfw::ProfileResult r{};
    r.err = mrfw::ProfileErr::bad_index;
    return r;
}

static char* joinprofile_word(char*& p) {
    while (*p == ' ') ++p;
    if (!*p) return nullptr;
    char* w = p;
    while (*p && *p != ' ') ++p;
    if (*p == ' ') *p++ = '\0';
    return w;
}

// `joinprofile list | set <1..4> layer= freq=<MHz> bw=<kHz> sf= [name="…"] | clear <1..4> | reset confirm`
// ★ THE OPERATOR TYPES THE SAME UNITS AS `join` — freq in MHz, bw in kHz. Only the STORED record is integral (Hz).
// ⛔ No JSON ack: `lib/console` owns the companion schemas and this slice must leave `lib/` untouched (D2). A
//    companion that wants presets gets them in the next slice that may edit lib/console.
void handle_joinprofile(const char* args, Print& out) {
    char buf[160]; size_t bn = 0; for (; args[bn] && bn < sizeof(buf) - 1; ++bn) buf[bn] = args[bn]; buf[bn] = '\0';
    char* p = buf;
    char* verb = joinprofile_word(p);
    if (!verb) goto usage;

    if (!strcmp(verb, "list")) {
        // ⛔ A TRAILING TOKEN IS REFUSED, ⛔ NOT IGNORED (C2). `joinprofile list extra` used to run a plain `list`,
        //    which tells the operator his word was understood when it was silently dropped — the same class of
        //    "success that isn't" this slice already refuses for a truncated name. Same rule on `clear` and `reset`.
        if (!mrfw::arg_tail_empty(p)) goto usage;
        mrnv::JoinBlob rec{};
        const mrfw::ProfileResult r = join_profile_service().list(rec);
        if (joinprofile_report_problem(r, out)) return;
        // ★ TWO DIFFERENT FACTS, ONE ANSWER: an ABSENT store and a valid store with four empty slots are both
        //   `NO PROFILES` to the operator. ⛔ A CORRUPT store is neither, and never reaches here.
        if (r.verdict == mrfw::ProfileVerdict::empty || mrfw::join_profile_count(rec) == 0) {
            out.println(F("> joinprofile NO PROFILES"));
            return;
        }
        for (uint8_t i = 0; i < mrnv::kJoinProfiles; ++i) {
            const mrnv::JoinProfile& pr = rec.prof[i];
            if (!pr.present) continue;
            const mrfw::JoinRequest jr = mrfw::join_request_from_profile(pr);   // the ONE Hz -> MHz/kHz authority (U2)
            out.print(F("> joinprofile ")); out.print((int)(i + 1));
            out.print(F(" layer=")); out.print((int)pr.layer);
            out.print(F(" freq=")); out.print(jr.freq_mhz, 4);    // 4 dp: 869.4625 renders EXACTLY, which is the point
            out.print(F(" bw=")); out.print(jr.bw_khz, 2);        // 2 dp: 62.50 / 41.67 are real LoRa bandwidths
            out.print(F(" sf=")); out.print((int)pr.routing_sf);
            out.print(F(" name=\""));
            for (uint8_t j = 0; j < pr.name_len && j < sizeof pr.name; ++j) out.write((uint8_t)pr.name[j]);
            out.println(F("\""));
        }
        return;
    }

    if (!strcmp(verb, "reset")) {
        // ⛔ A MISSING `confirm` REFUSES WITHOUT LOADING AND WITHOUT WRITING — the service enforces that; this only
        //    reports whether the word was typed.
        {
            const char* c = joinprofile_word(p);
            if (!mrfw::arg_tail_empty(p)) goto usage;      // `reset confirm extra` — refused, ⛔ never a silent reset
            const mrfw::ProfileResult r = join_profile_service().reset(c && !strcmp(c, "confirm"));
            if (joinprofile_report_problem(r, out)) return;
            out.print(F("> joinprofile reset ")); out.println(mrfw::profile_verdict_name(r.verdict));
            return;
        }
    }

    if (!strcmp(verb, "clear")) {
        {
            const char* n = joinprofile_word(p);
            if (!n) goto usage;
            if (!mrfw::arg_tail_empty(p)) goto usage;      // `clear 1 extra` — refused BEFORE any load or write
            // ⛔⛔ NOT `atol`: it parses a PREFIX, so `clear 2junk` used to CLEAR SLOT 2. The strict parse is in
            //     firmware_config_parse.h where the native suite drives it; the SERVICE still bounds 1..4.
            long slot = 0;
            const mrfw::ProfileResult r = mrfw::parse_index_strict(n, slot)
                                        ? join_profile_service().clear(slot)
                                        : joinprofile_bad_index();
            if (joinprofile_report_problem(r, out)) return;
            out.print(F("> joinprofile clear ")); out.print(n); out.print(' ');
            out.println(mrfw::profile_verdict_name(r.verdict));
            return;
        }
    }

    if (!strcmp(verb, "set")) {
        {
            const char* n = joinprofile_word(p);
            if (!n) goto usage;
            // ⛔⛔ NOT `atol`: `set 1x layer=… ` used to overwrite SLOT 1. The refusal is deferred to the ONE service
            //     call below rather than taken here, so `set 1x wibble=3` still names the unknown key — the more
            //     actionable of the two truths (§B212's rule). ⛔ ZERO writes on either path.
            long slot = 0;
            const bool slot_ok = mrfw::parse_index_strict(n, slot);
            PhyArgs pa{};                     // freq MHz / bw kHz (FRACTIONAL) / sf / layer — all four REQUIRED
            char nm[sizeof(mrnv::JoinProfile::name)];
            size_t name_typed = 0;              // ★ the TYPED length, which may EXCEED the slot — see below
            char* k; char* v;
            while (kv_next(p, k, v)) {
                if (phy_arg_take(pa, k, v, /*allow_layer=*/true)) continue;
                if (v && !strcmp(k, "name")) {
                    // ★★ COUNT WHAT WAS TYPED, COPY ONLY WHAT FITS. Handing the service the TRUE length is what lets
                    //    it REFUSE `name_too_long` (C2) instead of silently storing a truncated label — a truncated
                    //    name is exactly the "success that isn't" this project has already paid for.
                    for (const char* c = v; *c; ++c) {
                        if (name_typed < sizeof nm) nm[name_typed] = *c;
                        ++name_typed;
                    }
                    continue;
                }
                out.print(F("> joinprofile err bad/unknown key: ")); out.println(k); goto usage;
            }
            if (!(pa.has_freq && pa.has_bw && pa.has_sf && pa.has_layer)) goto usage;
            // ⛔ THE NARROWING GUARD, VERBATIM FROM `handle_join` AND LOAD-BEARING FOR THE SAME REASON: `PhyArgs::
            //    layer` is a `long`, so 257 would narrow to a perfectly valid 1 and store a profile for the WRONG
            //    layer. ⓘ It deliberately does NOT screen a NaN (`phy_args_in_range` accepts one — [[B216]]); the
            //    service refuses that at the profile boundary, which is where the integral record makes it fatal.
            if (!phy_args_in_range(pa, /*with_layer=*/true)) goto usage;
            {
                mrfw::JoinRequest rq{};
                rq.layer = (uint8_t)pa.layer; rq.freq_mhz = pa.freq_mhz;
                rq.bw_khz = pa.bw_khz; rq.routing_sf = (uint8_t)pa.sf;
                const uint8_t nlen = (uint8_t)(name_typed > 255 ? 255 : name_typed);
                const mrfw::ProfileResult r = slot_ok
                    ? join_profile_service().set(slot, rq, name_typed ? nm : nullptr, nlen)
                    : joinprofile_bad_index();
                if (joinprofile_report_problem(r, out)) return;
                out.print(F("> joinprofile set ")); out.print(n); out.print(' ');
                out.println(mrfw::profile_verdict_name(r.verdict));
                return;
            }
        }
    }

usage:
    out.println(F("> joinprofile err usage: joinprofile list | set <1..4> layer=<1..255> freq=<MHz> bw=<kHz 7..500> sf=<5..12> [name=\"<12 chars>\"] | clear <1..4> | reset confirm"));
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
        mr_ui_on_config_saved();   // §notify-every-save — site 4 of 7 (assigns none of the four covered fields; the rule is the point, not the field list)
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
// because the console text is user-visible and differs verb by verb.
// ⛔⛔ CORRECTED ([[B212]], 2026-08-18). This comment used to end *"including `team`'s own inconsistency
//    (`> team err bad/unknown key:` vs `> team new err:`), which is PRESERVED rather than tidied (C1)"* — and the
//    record matters: that inconsistency was a KNOWN, DELIBERATE artefact, not an oversight, so leaving it alone was
//    the right call for a refactor. ⇒ [[B212]] makes removing it a SANCTIONED FIX, not tidying: the `PhyTailMsgs`
//    set is shared by ALL THREE team forms (`team new`, `team <id>`, `team 0`), so a range error on a JOIN or a
//    LEAVE announced itself as `> team new err:` — ⛔ a message naming a subcommand the operator did not type is a
//    DEFECT, in the same family as the four false comments this arc has already had to correct. The third string is
//    now verb-neutral (`> team err:`) and matches its two siblings; ★ the caller's set is the ONE place these read
//    (`handle_team`, `:1368`), so there is no second spelling left to drift.
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
    // ⛔⛔ §PROV-TX §3.10 — `scratch` HELD THE PRIVATE KEY AS CLEARTEXT HEX AND IS `static`, so before this line it
    //    OUTLIVED THE COMMAND IN RAM (and every command after it, until a longer tail overwrote it). Wiped HERE, where
    //    it was filled, and AFTER the switch because `bad_key` points into it. ⓘ `rest` is deliberately NOT wiped: the
    //    tk tokens were removed from it by construction (that is what `split_team_key_tail` is for), so it carries the
    //    surviving PHY tokens only. The 32-byte binary halves in `pub`/`priv` belong to the caller's request and are
    //    wiped by `TeamRequest::wipe()` on every exit of handle_team.
    crypto_wipe(scratch, sizeof scratch);
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
        // than emit an incoherent export. ✅ T-K1's deferred question is now ANSWERED (§o3-key-lifetime, owner ruling
        // 2026-07-31): `set_team_id` CLEARS the pair, so `team new` then `team 0` no longer leaves a key behind with
        // team_id==0 — that state is unreachable and this branch is a pure defence-in-depth ordering guard. The
        // REACHABLE keyless case is the one below (`no_key`): a bare `team <id>` join, which must be re-granted.
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

// ============================================================ §PROV-TX — THE DEVICE BINDINGS ([[B207]], spec §3.1)
// ★★ THE ONLY THREE THINGS THE TRANSACTION NEEDS FROM THE DEVICE, and each is a THIN adapter on purpose: any decision
//    taken here would be unreachable by every automated gate (this TU is compiled by neither the native suite nor the
//    simulator), which is the whole reason the logic lives in `firmware_provisioning_service.h`.
// ⓘ The DURABLE seam is NOT re-bound: `device_cfg_store()` above is reused unchanged (U1) — one `/mrcfg` record, one
//   whole-record write, and the same "false = THE WRITE FAILED" contract `ConfigService` runs on.
// ★ These live INSIDE the `#if MR_N_LAYERS < 2` region, so the two feature axes are guaranteed present rather than
//   stubbed — asserted rather than assumed, because a silently-inert binding would make the transaction a no-op:
static_assert(MR_FEAT_TEAM == 1 && MR_FEAT_MOBILE == 1,
              "§PROV-TX: handle_team's region assumes BOTH planes are compiled in (MR_FEAT_TEAM 0 / MR_FEAT_MOBILE 0 "
              "arrive only with MR_PROFILE_GATEWAY, which sets MR_N_LAYERS=2 and compiles this whole region out). If "
              "that stops holding, the IProvLive binding below would silently bind to inert stubs — bind explicitly.");
namespace {
// ★ The CSPRNG seam. Same source the old inline mint used (`g_hal.rand_bytes` = the HW RNG on device, the simulator's
//   per-node deterministic stream in-sim), and the DRAW ORDER is preserved too — the team-id nonce first, then the
//   32-byte key scalar — so nothing about the entropy stream moves.
struct DeviceProvEntropy : mrfw::IEntropy {
    void fill(uint8_t* out, size_t n) override { g_hal.rand_bytes(out, n); }
};
// ★★ THE FOUR LIVE OPERATIONS, in the §3.2 order. Note what is NOT here: no `team_channel_key_adopt`, no
//    `team_channel_key_adopt_priv`, no `team_channel_key_mint`. All three are `bool` (fallible) and all three belong
//    to STAGING or to nothing — the post-save install is the `void` `team_channel_key_load`, which is documented as
//    *"boot restore from NV — VERBATIM, no re-derivation"* and is exactly the semantic step 6b needs.
struct DeviceProvLive : mrfw::IProvLive {
    // §clean-team: ONE core call does the whole switch — drop the OLD team's learned plane (_rt_team / _team_peer /
    // team liveness / the team KEY CACHE / team RREQ ledgers) and the stale team-DAD id, then adopt the new id LIVE.
    // ⓘ The `bool` return is DELIBERATELY discarded: the plan decided `membership_changed` at stage time from
    //   `NodeConfig::team_id` (the same comparison this function makes at `node.cpp:667`), and this call is only
    //   reached when that was true. Its other false arm — a build with no mobile plane — is refused at staging.
    void set_team(uint32_t team_id) override { (void)g_node.set_team_id(team_id); }
    // ★★ THE INFALLIBLE INSTALL, and it MUST come after `set_team`: `set_team_id` DESTROYS the team channel key by
    //    design (§o3-key-lifetime — a content key must not outlive the team it was granted for), so the reverse order
    //    would persist a key the node no longer holds. The pair handed in is already CANONICAL (derived + cross-checked
    //    at stage time) and is byte-identical to what the candidate persisted, which is why VERBATIM is correct here.
    void install_key(const uint8_t pub[32], const uint8_t priv[32]) override {
        g_node.team_channel_key_load(pub, priv, /*present=*/true);
    }
    // ★★ [[B209]] — RETUNE ONLY. ⓘ `layer_id` is read LIVE, exactly as the old inline call did
    // (`parse_phy_tail(…, c.leaf_id, …)`): the team PHY sets the CURRENT layer's radio floor.
    // ⛔ The call below was `mobile_register_phy`, whose contract is *retune + `mobile_request_home_service()` +
    //    immediate DISCOVER* — so a TEAM PHY tail silently AUTHORISED static-home attachment on a node configured
    //    `mobile_autoregister=false` (metal-confirmed on both bench nodes). A provisioning PHY apply is a PHY
    //    operation; it must not decide that this device wants a home. `mobile_retune_phy` is the retune-only seam.
    // ⓘ Nothing is lost by not kicking the FSM here: `fire_dad` (step 6d) is the team plane's explicit airtime
    //    operation, and the boot arm at `lib/core/node.cpp:588` still runs team-DAD for an auto-OFF team member.
    void apply_phy(const mrfw::ProvPhy& p) override {
        meshroute::LayerConfig phy{};
        phy.layer_id           = g_node.config().leaf_id;
        phy.routing_sf         = p.routing_sf;
        phy.freq_mhz           = p.freq_mhz;
        phy.bw_hz              = p.bw_hz;
        phy.allowed_sf_bitmap  = p.allowed_sf_bitmap;
        g_node.mobile_retune_phy(phy);
    }
    // ★★ THE AIRTIME OPERATION, and the reason a save failure must return before reaching it: §6.4's team-plane
    //    bootstrap self-assigns a `_team_local_id` with no static host needed — i.e. it TRANSMITS.
    void fire_dad() override { g_node.team_dad_fire(); }
};
}  // namespace
// Function-local statics: constructed on first CALL, so there is no cross-TU initialisation-order question (the same
// reasoning as `device_cfg_store()`). ONE service instance, and §UI-15 slice 5 is the OLED path that now reaches for
// it too — ⛔ never a second service over the same store, which would be two transactions with one durable seam.
// ⓘ It LOST its `static` for that reason and is declared in `firmware_config.h`; nothing else about it moved.
mrfw::ProvisioningService& prov_service() {
    static DeviceProvLive     live;
    static DeviceProvEntropy  ent;
    static mrfw::ProvisioningService s(device_cfg_store(), live, ent);
    return s;
}

// ★★ §UI-15 slice 5 — THE OLED PATH's TWO DEVICE FORWARDS (declared in firmware_config.h, which carries the boundary
//    argument). ⛔ NEITHER TAKES A DECISION: one gathers reads, the other performs one assignment.
// ⚠⚠ THE SNAPSHOT FILL IS A SECOND COPY OF `handle_team`'s, DECLARED RATHER THAN ACCIDENTAL — `tools/probe_prov_tx`'s
//    S11 pins those six assignments INSIDE `handle_team`'s own body (a check whose whole point is that a zeroed
//    snapshot would make every live comparison read "freq 0.0 / no key"), so routing the console through here would
//    turn that probe red for a refactor this slice is not allowed to make (C1). ★ THE TWO ARE EDITED TOGETHER: the
//    accessors, and the reasons for each one, are documented once at `handle_team` (`:1719`) and are not restated.
// ⚠ THE OUT-PARAMETER IS `out`, NOT `snap`, AND THAT IS DELIBERATE RATHER THAN A STYLE PREFERENCE: `probe_prov_tx`'s
//   C12/C13/C13b delete `handle_team`'s live reads BY EXACT TEXT and require a match count of ONE, so a second
//   `snap.live_… = g_node.…()` block in this file would make all three controls UNUSABLE — i.e. it would silently
//   disarm the instrument that proves the console fills its snapshot. ⓘ It fails LOUDLY (the runner reports
//   "match count=2") rather than passing vacuously, which is how this was found; the name keeps each control aimed at
//   the copy it was written for. (`out` is also this cluster's own out-parameter idiom — `ICfgStore::load(out)`.)
void prov_device_facts(mrfw::ProvSnapshot& out, mrfw::ProvPhyFloor& floor) {
    const meshroute::NodeConfig& c = g_node.config();
    out.mobile_reg_count       = g_node.mobile_reg_count();
    out.key_hash32             = g_node.key_hash32();
    out.live_freq_mhz          = g_node.active_freq_mhz();
    out.live_bw_hz             = g_node.active_bw_hz();
    out.live_routing_sf        = c.layers[0].routing_sf;
    out.live_allowed_sf_bitmap = c.layers[0].allowed_sf_bitmap;
    out.live_key_pub           = g_node.team_channel_pub();
    out.live_key_priv          = g_node.team_channel_priv();
    // The same two build defaults `handle_team` passes (§3.4): what a `0` in the persisted record resolves to.
    floor.freq_mhz             = (double)LORA_FREQ;
    floor.bw_hz                = meshroute::protocol::khz_to_hz(LORA_BW);
}
void prov_note_persisted_team_local_id(uint8_t v) { g_persist_team_local_id = v; }

// §PROV-TX — THE VOICE OF A NON-`applied` VERDICT, and it is a separate function for two reasons rather than one:
// U3 (`handle_team` stays parse -> request -> render, not a wall of strings) and because ONE guarded early return is
// what lets `tools/probe_prov_tx` and `tools/probe_board_ui`'s W17 pin *"notify ONLY on the applied arm"* as a compact
// source fact — the property is unreachable by any automated build (this TU is compiled by neither the native suite
// nor the simulator), so a shape a grep can state exactly is worth more here than a tidier inline switch.
// ⛔ EVERY ARM IS A REFUSAL OR A NON-EVENT: nothing was written, nothing was applied, and no airtime was spent. The
//    strings say so explicitly, because the old verb's failure line said the OPPOSITE (*"team is LIVE but NOT
//    persisted — will revert on reboot"*) and an operator who read it had no way to know what state the node was in.
// `-Wswitch` is `-Werror=switch` here, so both switches are `default`-less: a new verdict or a new `ProvErr` fails the
// build until its text is written, which is the same discipline `cfg_save_name` and `peer_put_name` are held to.
static void team_report_not_applied(const mrfw::ProvResult& res, Print& out) {
    switch (res.verdict) {
        case mrfw::ProvVerdict::refused:
            switch (res.err) {
                // U1: the role refusals speak through the ONE message set (`role_refused`), so `team <id>` is not a
                // second spelling of the O1/O2/R4 policy. `no_mobile_plane` reuses that same voice deliberately.
                case mrfw::ProvErr::role_refused:
                    (void)role_refused(res.role_refusal, F("> team err "), out);
                    return;
                case mrfw::ProvErr::no_mobile_plane:
                    (void)role_refused(meshroute::RoleSetRefusal::no_mobile_plane, F("> team err "), out);
                    return;
                case mrfw::ProvErr::key_on_leave:
                    out.println(F("> team err: tkpub=/tkpriv= make no sense on `team 0` (leave)"));
                    return;
                // ★★ v4 OWNER RULING: leaving a team PRESERVES the PHY, so a PHY argument on `team 0` is REFUSED
                //    LOUDLY — ⛔ not honoured, ⛔ not ignored, ⛔ not partially parsed. Same shape as the refusal above.
                case mrfw::ProvErr::phy_on_leave:
                    out.println(F("> team err: freq=/sf=/bw= make no sense on `team 0` (leave) — leaving a team PRESERVES the current PHY."));
                    out.println(F(">   NOTHING changed. To retune, leave the team first (`team 0`) and then set the PHY (`mobile register freq=… sf=… bw=…`)."));
                    return;
                case mrfw::ProvErr::key_degenerate:
                    out.println(F("> team err: tkpriv= REFUSED — not a valid X25519 scalar (all-zero or degenerate). Team NOT joined; NOTHING changed."));
                    return;
                // ⚠ THE OLD CODE COULD ONLY REACH THIS REFUSAL **AFTER** RETUNING THE RADIO. It is now caught at stage
                //   time, where refusing is free — and it is a check the SYNTAX parser structurally cannot make.
                case mrfw::ProvErr::key_mismatch:
                    out.println(F("> team err: tkpub= REFUSED — it is not the public key of the supplied tkpriv= (the two halves do not match). Team NOT joined; NOTHING changed."));
                    return;
                case mrfw::ProvErr::keygen_failed:
                    out.println(F("> team err: team channel keygen FAILED (crypto RNG returned no entropy). Team NOT minted."));
                    return;
                case mrfw::ProvErr::incomplete_phy:
                    out.println(F("> team err: incomplete PHY — need freq, routing_sf(5..12), sf_list(DATA SF), bw."));
                    out.println(F(">   set them inline: `team new freq=869.0 sf=7 bw=125` — ALL members MUST use the SAME freq/sf/bw."));
                    return;
                case mrfw::ProvErr::id_unavailable:
                    out.println(F("> team err: could not mint a usable team id (every draw came back 0 or this node's CURRENT team). NOTHING changed — retry."));
                    return;
                case mrfw::ProvErr::nv_load_failed:
                    out.println(F("> team err nv_load_failed — the config record could not be read, so the non-team fields cannot be preserved. NOTHING changed."));
                    return;
                case mrfw::ProvErr::nv_save_failed:   // carried by the nv_failed VERDICT below, never by this arm
                case mrfw::ProvErr::none:             // unreachable for a refusal; listed so -Wswitch stays exhaustive
                    return;
            }
            return;
        // ★★★ THE POINT OF THE WHOLE SLICE. This arm used to read *"team is LIVE but NOT persisted — will revert on
        //     reboot"* and then CARRY ON; now the write is the commit point, so a failure means nothing happened at all.
        // ⛔⛔ WHAT THIS LINE MAY NOT CLAIM, AND THE CLAIM IS NOT A WORDING PREFERENCE. An earlier version ended by
        //     asserting THE FLASH HAD NOT MOVED — ⛔ NOT ESTABLISHED. A backend can fail AFTER a partial write; that is
        //     precisely [[B193]]'s open question, and no host test can settle it (spec §5.1). What IS established is
        //     what the transaction itself did: it attempted EXACTLY ONE write, applied NOTHING when that write reported
        //     failure, and reached no `fire_dad`. ⇒ say that, and nothing about the flash.
        case mrfw::ProvVerdict::nv_failed:
            out.println(F("> team err nv_save_failed — persistence FAILED: NO live state was applied (team, role, keys and PHY are ALL as they were) and NO AIRTIME was spent. Retry, or check flash."));
            return;
        // ★★ A HARD REQUIREMENT, not an optimisation (§3.7): a same-team request that changes nothing performs ZERO
        //    saves and ZERO live applies. ⓘ `mrnv::save` would have coalesced the byte-identical record anyway — the
        //    point is that the TRANSACTION decides it explicitly instead of inheriting it from a lower layer.
        // ⚠ AND THE WORDING IS NOW EXACT ([[B207]] QG round 3): this verdict requires the STORED RECORD **AND** the
        //   LIVE radio/key to already match everything the command named. It used to be reachable with the record
        //   matching while the RADIO sat on a different PHY — the line below then said "nothing was applied" about a
        //   request whose whole point was to apply something.
        case mrfw::ProvVerdict::no_change:
            { char nx[9]; snprintf(nx, sizeof nx, "%08lX", (unsigned long)res.team_id);
              out.print(F("> team: no change — already team_id=0x")); out.print(nx);
              out.println(F(", and the stored record AND the live radio/key already match what you asked for. Nothing was written and nothing was applied.")); }
            return;
        case mrfw::ProvVerdict::applied:   // never routed here (the caller's guard), and it prints NOTHING if it were
            return;
    }
}

// §mobile 6.1: FNV-1a over (key_hash32 ‖ nonce) = the 32-bit team_id (team_fnv1a32, firmware_config_parse.h).
// `team new` = MINT a fresh team_id = hash(our key ‖ HW-RNG nonce). `team <id>` = JOIN an existing team. `team 0` = leave.
void handle_team(const char* args, Print& out) {
    while (*args == ' ') ++args;
    const meshroute::NodeConfig& c = g_node.config();
    uint32_t t = 0;              // the TARGET form's operator-typed id; the `new` form leaves it 0 (see below)
    const char* phy_args = nullptr;
    // §team-ch-key (T-K1b): the THIRD subcommand, beside `new` and `<id>` (T-K3's `grantkey` is the fourth).
    // ★ ANSWERED FIRST, BEFORE the numeric parse below — that is a safety requirement, not ordering taste. `strtoul`
    // consumes ZERO digits from a non-numeric tail and returns 0 (verified, not assumed), so ANY subcommand that fell
    // through to it would be read as `team 0` = LEAVE THE TEAM instead of running.
    // ★★ §team-target (BUG FIX 2026-07-30, widened by §team-target-whole and §team-target-range 2026-07-31): the residual
    // hole that ordering could NOT close — a NEAR-spelling (`team exportky`, `team grantky`, `team nwe`) matches none of
    // these strncmps and used to reach the numeric parse, i.e. LEAVE THE TEAM. It is now refused by parse_team_target below
    // (firmware_config_parse.h: a target must lead with a digit, the whole token must parse, AND the value must fit 32 bits
    // — `team 0x`/`team 08` were silent leaves, `team 88A672BA` a silent join of team 88, and `team 4294967296` joined
    // garbage team 0xFFFFFFFF on the boards). Ordering is now defence-in-depth rather than the only guard; both remain
    // deliberate.
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
        // ★★ §PROV-TX §3.5: THE ID IS NO LONGER MINTED HERE. It used to be `t = team_fnv1a32(key_hash32, nonce)` on
        //    this line, straight from `team_fnv1a32`, which HAS NO ZERO GUARD — so a 0 made `team new` mint a key and
        //    then execute `team 0` = LEAVE, skipping every `t != 0` guard and doing the opposite of what was asked
        //    (~1 in 2^32, structurally reachable). ⇒ generation moved into the shared builder, which resamples over a
        //    bounded loop until the id is neither 0 NOR THE CURRENT TEAM (the second exclusion matters too: a
        //    regenerated current id would silently make `team new` a same-team RE-KEY), and refuses loudly otherwise.
        //    `t` stays 0 on this arm and is deliberately unused — the id to report comes back in `ProvResult`.
        phy_args = args + 3;   // §mobile 6.4: `team new [freq=<MHz> sf=<5-12> bw=<kHz>]` — optional team PHY
    } else if (parse_team_target(args, t, phy_args)) {
        // §6.4: `team <id> [freq= sf= bw=]` — a JOIN can set the shared team PHY too (mirrors `team new`). phy_args =
        // strtoul's endp, i.e. whatever follows the digits. ★ §team-target: entry now REQUIRES a leading digit, a
        // fully-consumed token AND a value inside 32 bits, so a mistyped subcommand can no longer arrive here as `team 0`
        // (= leave), a 0x-less hex id can no longer arrive as its decimal prefix, and an over-wide token can no longer
        // arrive as whatever this ABI's `unsigned long` saturated or truncated to — see parse_team_target for the
        // measurement (`t` is therefore always a value the operator actually typed).
    } else if (args[0]) {
        // ★★ §team-target (BUG FIX 2026-07-30) — C2: a non-numeric, non-subcommand tail is REFUSED LOUD. It must never
        // reach the numeric parse (strtoul -> 0 -> LEAVE) and must never be a silent no-op either. Nothing has been
        // touched at this point: no NV load/save, no set_team_id, no key mint/adopt, no PHY retune.
        // ★ §team-target-range (B17): the message states the RULE SET, not a diagnosis of one clause — deliberately, because
        // this branch cannot say WHICH clause refused (parse_team_target returns bool). The old wording named only the two
        // SYNTAX clauses and was therefore FALSE for the range case (`team 4294967296` does begin with a digit and does parse
        // wholly) — the same factually-wrong-message trap B1 had to rewrite. Every sentence below is true of all three clauses.
        out.print(F("> team err: bad target `")); out.print(args); out.println(F("` — a team id must be a WHOLE numeric token that FITS IN 32 BITS."));
        out.println(F(">   It must BEGIN WITH A DIGIT, the ENTIRE token must parse, and the value must be <= 4294967295 (0xFFFFFFFF)."));
        out.println(F(">   ★ A HEX id needs its `0x`: `team 88A672BA` is not team 88A672BA — write `team 0x88A672BA`."));
        out.println(F(">   NOTHING changed — team_id, the team channel key and NV are all as they were. (Before these checks a"));
        out.println(F(">   mistyped subcommand parsed as `team 0` and LEFT THE TEAM, a 0x-less hex id JOINED THE WRONG TEAM, and"));
        out.println(F(">   an out-of-range id JOINED GARBAGE TEAM 0xFFFFFFFF on the 32-bit boards — or LEFT THE TEAM on a 64-bit host.)"));
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
    // ★★★ §PROV-TX ([[B207]], owner-ruled 2026-08-17) — FROM HERE THIS VERB IS **ONE TYPED TRANSACTION**.
    // ⛔⛔ WHAT USED TO BE HERE, AND WHY IT IS GONE RATHER THAN REORDERED. This block performed SIX LIVE MUTATIONS
    //    BEFORE its `mrnv::save` — the PHY retune (which also kicks the mobile FSM), `team_channel_key_adopt`,
    //    `team_channel_key_mint`, `set_team_id`, `team_channel_key_adopt_priv` and ★★ `team_dad_fire()`, which SPENDS
    //    AIRTIME — and its own failure line admitted the consequence: *"team is LIVE but NOT persisted — will revert on
    //    reboot"*. On an off-grid safety device a `team new` whose flash write failed PRESENTED AS SUCCESS and then lost
    //    membership at the next power cycle. ⛔ AND "just move the save up" was structurally impossible: the persisted
    //    candidate was DERIVED from having already mutated (it read `canonical_node_id()` because DAD may have moved it,
    //    `team_local_id()` because DAD assigned it, `is_mobile` because the switch may have promoted it, and the key the
    //    adopt/mint had installed). ⇒ the candidate had to become computable WITHOUT mutating, which is what
    //    `mrfw::project_team` + `mrfw::stage_team_candidate` do.
    // ★ EVERYTHING BELOW THIS LINE UNTIL `apply_team` IS PARSING ONLY — no NV read, no retune, no key install, no
    //   `set_team_id`, no entropy draw — so every refusal returns with all five domains (PHY, role, team, keys, NV)
    //   untouched and ZERO airtime spent. The five refusals that used to live here (role, `team 0 tkpub=`, incomplete
    //   PHY, key adopt, key mint) are now DECISIONS IN THE SERVICE, where the native suite can reach them; this
    //   function keeps only their voice.
    mrfw::TeamRequest rq{};
    // ★ THE SECRET'S SCOPE GUARD: `rq` carries a PRIVATE KEY, and this wipes both halves on EVERY exit below —
    //   including the two parse-failure returns. ⓘ The old code never wiped its `tk_priv` frame buffer at all (§3.10).
    struct ReqGuard { mrfw::TeamRequest& q; ~ReqGuard() { q.wipe(); } } rq_guard{rq};
    rq.mint    = mint_form;
    rq.team_id = mint_form ? 0u : t;
    // The BUILD floor a `0` in the persisted record resolves to (§3.4). ⚠ Passed IN because the service is board-free;
    // these are the same two defaults `handle_gateway`'s validate and the §nv-ritual seed use (U1). ⓘ On device a fresh
    // chip does not reach them — `nv_load_stamped` SEEDS from the live config — so a 0 means the record truly holds 0.
    rq.floor.freq_mhz = (double)LORA_FREQ;
    rq.floor.bw_hz    = meshroute::protocol::khz_to_hz(LORA_BW);
    // §team-ch-key (T-K1): peel `tkpub=`/`tkpriv=` off the tail FIRST — parse_phy_tail below refuses unknown keys.
    // ⛔ This validates SYNTAX ONLY (64 hex digits, both-or-neither). It never proves the public half belongs to the
    //    private one — that cross-check is the TRANSACTION's, at stage time, because the primitive that used to do it
    //    (`team_channel_key_adopt`) is fallible and may no longer run after the save (§3.6).
    static char tk_rest[96];   // STATIC (see parse_team_key_tail): keeps the console frame small AND outlives this block for the PHY parse below
    if (phy_args && *phy_args) {
        const mrfw::TeamKeyTail r = parse_team_key_tail(phy_args, tk_rest, sizeof tk_rest, rq.key_pub, rq.key_priv, out);
        if (r != mrfw::TeamKeyTail::none && r != mrfw::TeamKeyTail::ok) return;   // reported; NOTHING has been touched
        rq.key_supplied = (r == mrfw::TeamKeyTail::ok);
        phy_args = tk_rest;                             // the tail parse_phy_tail sees has the team-key tokens REMOVED (it may now be empty)
    }
    // §mobile 6.4 Fix 6: the SHARED team PHY, so teammates hear each other (and a member can later register with a
    // compatible static network). Omitted -> the current PHY is carried through.
    // ⛔⛔ NO LONGER GATED ON THE OLD ROLE, and that gate was a live C2 defect (§1.2.1): `if (phy_args && *phy_args &&
    //    c.is_mobile)` meant a STATIC node's `team new freq=869 sf=7 bw=125` SILENTLY DISCARDED the PHY arguments, and
    //    the incomplete-PHY check then ran against live values. Parsing mutates nothing, so it now runs
    //    unconditionally and the TRANSACTION gates the result on the PROJECTED role — which is what makes a static
    //    node promoted BY THIS VERB have its PHY honoured.
    // ★★★ [[B212]] (2026-08-18) — THE SPECIFIC `team 0` REFUSAL MUST WIN OVER THE GENERIC RANGE ERROR.
    //     `parse_phy_tail` below insists on a COMPLETE, IN-RANGE triplet, so a PARTIAL leave tail (`team 0 freq=868`)
    //     died in the parser and this function RETURNED — the request never reached the transaction, and
    //     `ProvErr::phy_on_leave` (`firmware_provisioning_service.h:393`, checked before role/id/projection) never
    //     ran. Metal-confirmed: the operator was told *"freq 100..1000 MHz…"*, i.e. the WRONG diagnosis of a value
    //     that was in fact fine — what is wrong is asking for a PHY on a leave at all.
    // ★ THE PRE-SCAN IS PURE AND NATIVELY TESTED (`mrfw::classify_phy_tail`, firmware_config_parse.h) — it decides
    //   only whether every token is a recognised PHY KEY, ⛔ never whether its VALUE is in range: leaving never
    //   accepts a PHY, so `team 0 freq=99999` is prohibited PHY, not a range error.
    // ⛔⛔ SCOPED TO THE LEAVE FORM, and that is a CORRECTNESS requirement, not an optimisation: `kv_next` tokenises
    //    IN PLACE, so a scan of the live tail would destroy what `parse_phy_tail` still needs on the join/mint
    //    paths. It runs only where no further parse follows, and even there it scans a COPY (`phy_scan`).
    // ⛔ MIXED TAILS KEEP THE PARSER'S ERROR: `team 0 freq=868 wibble=3` must still name `wibble` — the unknown
    //    token is the more actionable complaint and must not be swallowed by the leave rule.
    // ★ ONE MESSAGE AUTHORITY (U1): this block sets a REQUEST FIELD and prints NOTHING. The refusal text belongs to
    //   `team_report_not_applied` (`:1169`) and is not re-spelled here.
    const bool leave_form = !mint_form && t == 0;
    bool phy_refusal_to_transaction = false;
    if (phy_args && *phy_args && leave_form) {
        static char phy_scan[96];   // STATIC for the same reason as `tk_rest` above: handle_team's frame already
                                    // carries a ~272 B mrnv::Blob (the do_post_ack stack-overflow lesson), and
                                    // console dispatch is single-threaded.
        if (mrfw::classify_phy_tail(phy_args, phy_scan, sizeof phy_scan) == mrfw::PhyTailKeys::phy_only) {
            rq.phy.present = true;              // ⇒ project_team answers `phy_on_leave` and NOTHING is written
            phy_refusal_to_transaction = true;  // ⇒ skip the parser entirely; its range check is what masked this
        }
    }
    if (phy_args && *phy_args && !phy_refusal_to_transaction) {
        const PhyTailMsgs msg{ F("> team err bad/unknown key: "),
                               F("> team err: PHY args need freq= (freq=<MHz> sf=<5-12> [bw=<kHz>])"),
                               F("> team err: freq 100..1000 MHz, sf 5..12, bw 7..500 kHz") };
        meshroute::LayerConfig phy{}; double bw = 0.0;
        const PhyTail r = parse_phy_tail(phy_args, c.leaf_id, msg, out, phy, bw);
        if (r == PhyTail::error) return;                           // reported; NOTHING has been touched
        if (r == PhyTail::ok) {                                    // `none` (empty tail, e.g. `team 0`) = keep the current PHY
            rq.phy.present           = true;
            rq.phy.freq_mhz          = phy.freq_mhz;
            rq.phy.routing_sf        = phy.routing_sf;
            rq.phy.bw_hz             = phy.bw_hz;
            // ★★★ [[B211]] — THE DATA `sf_list` IS **NOT** SENT, AND THE OMISSION IS THE FIX. `parse_phy_tail` builds a
            //     fresh `LayerConfig{}` and sets `allowed_sf_bitmap = 1u << pa.sf` (`:885`), so copying it here made the
            //     ROUTING/control `sf=` also COLLAPSE the DATA SF set to that one value — unrequested, unreported and
            //     PERSISTED (metal-confirmed: a node booted `data sf = 6,7` came back `data sf = 7` and survived a
            //     power-cycle). ★ `lib/core/node.h:302-305` defines team-PHY compatibility as freq/bw/routing_sf/cr and
            //     says **NOT sf_list — F-SF-1 keeps that across registration** ⇒ team coherence never needed it.
            // ⛔⛔ AND THE FIX IS HERE, NOT IN THE PARSER: `parse_phy_tail` is SHARED with `handle_mobile` (`:1458`), so
            //     changing it would silently alter `mobile register` semantics — which the owner's ruling excludes.
            // ★ `0` = "the operator named no sf_list", and it is a SAFE sentinel needing no new field: an empty set
            //   blocks DATA entirely and is already refused (`ProvErr::incomplete_phy`,
            //   `firmware_provisioning_service.h:551`), and the [[data-sf-removed]] ruling makes it illegal as a value
            //   ⇒ it can never be a legitimate request. `stage_team_candidate` RESOLVES it from the PERSISTED RECORD
            //   before either comparison runs, and `res.phy.allowed_sf_bitmap` carries the resolved set back for the
            //   echo below — so what is reported is what actually landed.
            rq.phy.allowed_sf_bitmap = 0;
            rq.phy.bw_khz            = bw;                         // the RAW operator value, for the echo line only (no second rounding)
        }
    }
    mrfw::ProvSnapshot snap{};
    snap.mobile_reg_count = g_node.mobile_reg_count();   // O2: the hosted-guest count `role_set_refusal` rules on
    snap.key_hash32       = g_node.key_hash32();         // §mobile 6.1: team_id = FNV-1a(key_hash32 ‖ nonce)
    // ★★★ THE LIVE PHY AND THE LIVE KEY ([[B207]] QG round 3) — the reads that stop an EXPLICIT request from being
    //     discarded because NV happens to hold its value already. `mobile register freq=…` retunes the radio and moves
    //     `_cfg.layers[0]` WITHOUT persisting (`Node::adopt_mobile_phy`, `lib/core/node.cpp:869`), so the record and
    //     the radio genuinely diverge on this device — and before this the transaction could only see the record.
    // ⓘ WHY THE ADAPTER AND NOT THE SERVICE: `firmware_provisioning_service.h` is compiled by the native suite, which
    //   has no `g_node` and no board — it must stay pure (the same split `mobile_reg_count`/`key_hash32` already use).
    // ★ EXISTING accessors only (U1): `active_freq_mhz()`/`active_bw_hz()` return the EFFECTIVE carrier (per-layer
    //   override or the global) the radio really flies on; `layers[0]` is this node's only leaf (`MR_N_LAYERS < 2`) and
    //   is the pair `adopt_mobile_phy` writes; `team_channel_pub()`/`team_channel_priv()` return nullptr — never a
    //   zero buffer — when no key is held, which is how "no live key" reaches the service without a second flag.
    // ⛔ NO COPY OF THE PRIVATE KEY IS TAKEN: the snapshot holds POINTERS into the node's own storage, so this frame
    //   never becomes a second unwiped home for the team secret (§3.10).
    snap.live_freq_mhz          = g_node.active_freq_mhz();
    snap.live_bw_hz             = g_node.active_bw_hz();
    snap.live_routing_sf        = c.layers[0].routing_sf;
    snap.live_allowed_sf_bitmap = c.layers[0].allowed_sf_bitmap;
    snap.live_key_pub           = g_node.team_channel_pub();
    snap.live_key_priv          = g_node.team_channel_priv();
    // ★★★ THE COMMIT. Validate + project + stage -> ONE `mrnv::save` -> and only then set_team / install key / retune /
    //     DAD, in that order. ⓘ `c` is a REFERENCE to the live config; it is read ONLY inside the staging half (the
    //     role projection and the membership comparison), strictly before the post-save block can move it.
    const mrfw::ProvResult res = prov_service().apply_team(rq, c, snap);
    // ---- and ONLY NOW is anything printed. §1.2.4: `ADOPTED`, `MINTED` and `> team PHY:` all used to print BEFORE
    //      the save, which is exactly what the OLED design's §3.6.5 forbids ("no screen may claim success before the
    //      save returns") — the console owes the same. One typed verdict in, one report out.
    if (res.verdict != mrfw::ProvVerdict::applied) { team_report_not_applied(res, out); return; }
    mr_ui_on_config_saved();   // §notify-every-save — site 5 of 7. ★ NOW ON THE SUCCESS ARM ONLY, and only after the
                               //   transaction reported `applied` — i.e. the write both HAPPENED and SUCCEEDED, which
                               //   is the rule's exact condition. ⛔ `no_change` performs no write, so it must NOT
                               //   notify: an OLED draft told the record moved when it did not is [[B194]] inverted.
    // ★★★ SYNC THE TEAM-DAD PERSIST TRACKER TO WHAT THE RECORD NOW HOLDS. ⛔ NOT OPTIONAL BOOKKEEPING — without it a
    //     newly assigned `team_local_id` CAN NEVER REACH NV. `g_persist_team_local_id` is the change-detector
    //     `persist_cfg_if_needed()` compares the live team-DAD id against (`fw_main.cpp:1030`), and it is written in
    //     only two places: the boot restore (`:797`) and that function's own successful save (`:1042`). The
    //     transaction's `ICfgStore::save` touches NEITHER — so the tracker was still holding the OLD team's id while NV
    //     now holds 0, and if team-DAD then happened to pick THE SAME NUMERIC ID the old team used, `team_changed` was
    //     FALSE, nothing was persisted, NV STAYED 0, and the next boot needlessly re-DAD'd.
    // ⓘ WHY HERE AND NOT IN THE SERVICE: the service is compiled by the native suite, which has no `fw_main` and no
    //   globals — the plumbing must not leak into it. It REPORTS the value it wrote; the adapter assigns it (U1/C3).
    // ⓘ WHY AFTER `apply_team` RETURNS IS STILL "BEFORE DAD" IN THE ONLY SENSE THAT MATTERS: the tracker is read by
    //   exactly one caller, `persist_cfg_if_needed()` at the loop tail (`fw_main.cpp:1553`), which cannot run until
    //   this verb returns (single-threaded loop). So the live id team-DAD assigns is compared against the SAVED 0 and
    //   is persisted promptly, which is the outcome design v2 asked for.
    g_persist_team_local_id = res.persisted_team_local_id;
    // ★★ [[B211]] — THE ECHO NOW NAMES ALL FOUR PHY FIELDS. It used to print THREE while the request silently altered a
    //    FOURTH it never mentioned (C2). `res.phy.allowed_sf_bitmap` is the set the transaction RESOLVED and applied —
    //    the operator's own list when the tail named none — so this reports WHAT LANDED, not what was asked for.
    // ⛔ THE FORMATTER IS THE EXISTING ONE (U1): `mrfw::print_sf_list` (`firmware_commands.cpp:250`, declared in
    //    `firmware_commands.h:46`), the same one `dump_cfg` and the boot banner use — ⛔ no third bitmap-to-text
    //    implementation. ★ AND IT IS PASSED `out`, never a global: its own comment records the defect where a formatter
    //    reached past the sink it was given and half a line went to USB instead (§B95).
    if (res.phy.present) {
        out.print(F("> team PHY: freq=")); out.print(res.phy.freq_mhz, 3);
        out.print(F(" sf=")); out.print(res.phy.routing_sf);
        out.print(F(" bw=")); out.print(res.phy.bw_khz, 2); out.print(F(" kHz sf_list="));
        mrfw::print_sf_list(out, res.phy.allowed_sf_bitmap); out.println();
    }
    // ★ The owner's ruling that a CREATOR ALWAYS ENDS UP HOLDING A KEYPAIR is now guaranteed by the candidate plus a
    //   `void` install, not by ordering luck — so v1's stash-and-re-apply dance around `set_team_id` is gone entirely.
    if (res.key_action == mrfw::KeyAction::install)
        out.println(res.key_minted ? F("> team channel key: MINTED (X25519)")
                                   : F("> team channel key: ADOPTED (from tkpub=/tkpriv=)"));
    // ★★ §role-model / B28 constraint 3 — REPORT the automatic promotion; never flip the role silently. `is_mobile`
    //    changes beaconing, home registration, DAD and relay behaviour, and it is ON THE WIRE (beacon bit 0x20 / J bit
    //    0x40). ⓘ The value is the PROJECTED `RoleFix::forced_mobile` the builder read off the real `role_enforce`,
    //    reported only on this arm — never inferred from a live comparison across a mutation.
    if (res.role_promoted)
        out.println(F("> role -> MOBILE (automatic: a team member IS a mobile — reachable by team_local_id, not by a static node_id). `team 0` leaves the team; the role then STAYS mobile."));
    char tx[9]; snprintf(tx, sizeof tx, "%08lX", (unsigned long)res.team_id);
    out.print(F("> team -> team_id=0x")); out.println(tx);
    // ★★ [[B210]] — THE LINE IS GATED ON THE TRANSACTION'S OWN AIRTIME DECISION, not on the shape of the result.
    //    It used to read `res.team_id != 0 && g_node.config().is_mobile`, which is true of EVERY applied team command
    //    on a mobile — so a same-team re-key or PHY-only re-apply printed `team-DAD: local_id=N` although membership
    //    never changed and nothing was ever transmitted. Metal-confirmed (bench 27.5/27.8): it printed on both
    //    invocations while `team_local_id` stayed 32 and the `»tx BCN` burst a real DAD produces never appeared, and
    //    it MISLED a bench check that used the line's absence as its "no airtime" discriminator.
    // ★ `res.dad_fired` is set at exactly one place — `if (plan.fire_dad) { _live.fire_dad(); r.dad_fired = true; }`
    //   (`firmware_provisioning_service.h:716`) — so the line now means what it says: DAD RAN. `fire_dad` is
    //   `membership_changed && team_id != 0 && projected_is_mobile` (`:619`), which STRICTLY IMPLIES both halves of
    //   the old gate; ⛔ re-adding them would be dead weight that hides where the decision is really made.
    // ★★ THE ID IS STILL READ LIVE, DELIBERATELY. `res.persisted_team_local_id` is NOT a substitute: it carries what
    //    the CANDIDATE wrote, which is 0 on precisely the membership-change case this line now prints (design v2 —
    //    `team_local_id = 0` means DAD-pending), so it would print `local_id=0` every time. DAD has just run one
    //    statement earlier inside `apply_team`, so the LIVE id is the fresh one.
    // ⓘ ACCEPTED CONSEQUENCE, not an oversight: on a same-team apply the operator no longer sees the id on this line.
    //   It is a `cfg` / `status` fact; a line labelled `team-DAD` must mean DAD. ⛔ No substitute line is added here.
    if (res.dad_fired) { out.print(F("  team-DAD: local_id=")); out.println(g_node.team_local_id()); }
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
    // ★ §MH-S4 §4.3 — `mobile unregister`: end the current volatile attachment session and return to `dormant`.
    // ⛔ Adds NO deregistration wire message (§4.3): the old home ages the row out under §9. ⚠ Tested BEFORE
    //    "register" would be reached is unnecessary (strncmp("register",8) cannot match "unregister"), but it is
    //    placed as its own verb rather than as `register off` so the grammar reads as the spec writes it.
    if (!strcmp(args, "unregister")) {
        g_node.mobile_unregister();
        out.println(F("> mobile unregister: home-service request cleared — attachment dormant, timers cancelled (no wire message; the old home ages the row out)"));
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
        // ★★★ §MH-S4 §4.1/§10 — `registered` IS NOW `mobile_attached()`, NOT `mobile_registered()`. A mobile whose
        // CLAIM is still unconfirmed reports `registered:false` + `attachment:"claiming"`, so a user can never see
        // a false registration during confirmation (§7.1's closing requirement). The home/local/epoch block below is
        // still filled from the PROVISIONAL attachment whenever one exists, because those are the values the node is
        // actually operating under and hiding them during `claiming` would make the state unreadable.
        m.registered = g_node.mobile_attached();
        if (g_node.mobile_registered()) {
            m.home = g_node.mobile_home_id(); m.local = g_node.mobile_local_id();
            m.epoch = g_node.mobile_reg_epoch(); m.home_layer = g_node.mobile_home_layer();
        }
        m.autoregister = c.mobile_autoregister;
        // ---- §MH-S4 §10: the two planes, REPORTED SEPARATELY, plus the confirmation age and the diagnostics ----
        m.attachment      = meshroute::Node::attach_state_name(g_node.mobile_attach_state());
        m.home_link       = meshroute::Node::home_link_name(g_node.mobile_home_link());
        m.last_result     = meshroute::Node::attempt_result_name(g_node.mobile_last_result());
        m.home_desired    = g_node.mobile_home_desired();
        m.home_confirmed  = g_node.mobile_home_confirmed_ever();
        // ★★★★ §MH-S4b — THE `static_cast<uint32_t>` IS GONE. `mobile_home_confirm_age_ms()` returns `uint64_t`
        // deliberately (the +0 u32 variant of its backing stamp was measured and DECLINED under M3), and this cast
        // threw those bits away one line before they were printed: the displayed age WRAPPED at ~49.7 days, which is
        // exactly the failure the 64-bit state was chosen to prevent. `MobileStatusFields::home_confirm_age_ms` is
        // now u64 too and `write_mobile_status` serializes it with `i64`, so the value is 64 bits end to end.
        m.home_confirm_age_ms = g_node.mobile_home_confirm_age_ms();
        m.claim_retries   = g_node.mobile_claim_retries();
        m.claim_retry_max = meshroute::protocol::presence_claim_max_retries;
        m.claim_solicited = g_node.mobile_claim_solicited();   // §MH-S4b §7.1 step 3: "asked, waiting" vs "will ask" (rendered only while `claiming`)
        m.retry_window_ms = g_node.mobile_retry_window_ms();   // §10 current retry window (§5.2's backoff accumulator)
        m.offers          = g_node.mobile_offers_n();
        m.scan_idx        = g_node.mobile_scan_idx();
        m.scan_count      = g_node.mobile_scan_count();
        m.candidates      = g_node.mobile_candidate_count();
        m.verified_candidates = g_node.mobile_verified_candidate_count();   // §MH-S5 §10 / [[B154]]: the count a voluntary re-home may actually choose from (§8.1 authority + §8.2 freshness)
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
    out.println(F("> mobile err usage: register [freq= sf= bw= | scan] | unregister | gateways | query <gw> | status"));
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
    mr_ui_on_config_saved();   // §notify-every-save — site 6 of 7, and THE BLOCKER this rule was written for: the `Blob{}` above RESETS ALL FOUR covered fields to 0, so an open draft must show `CFG! RELOAD` immediately
    provision_apply_live(b, /*do_dad=*/false);                            // unprovisioned + idle (no DAD)
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
    memset(&admin, 0, sizeof admin);                          // discard the derived keypair (best-effort local wipe) — ⛔ NOT moved by §notify-every-save: the wipe must happen on BOTH arms
    if (!saved) { out.println(F("> password err: nv_save_failed")); return; }
    mr_ui_on_config_saved();   // §notify-every-save — site 7 of 7, on the SUCCESS side of the verdict this site already captured (admin fields only, so the comparison is a no-op — the rule is the point)
    out.print(F("> admin pubkey pinned (fp "));             // print only a 4-byte fingerprint, NEVER the pubkey/pw
    const uint8_t* pk = g_node.admin_pubkey();
    for (int i = 0; i < 4 && pk; ++i) { char hx[3]; snprintf(hx, sizeof hx, "%02X", pk[i]); out.print(hx); }
    out.println(F(")"));
}
#endif

}  // namespace mrfw
