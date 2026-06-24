// MeshRoute — src/device_nv.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Persist the device's runtime state to on-chip flash so it survives reboot. TWO records:
//   - `/mrcfg` (Blob)   = RADIO/PROTOCOL CONFIG + the short `node_id` (a `cfg set` over the console).
//   - `/mrid`  (IdBlob) = the 32-byte identity master seed + name (HW-RNG on first boot; `regen`). The
//                         keypair / key_hash32 are DERIVED from the seed at boot (lib/core/identity).
// Backends:
//   nRF52 (Adafruit core) -> Adafruit_LittleFS / InternalFS (files "/mrcfg", "/mrid")
//   ESP32  (Heltec)        -> Preferences / NVS (namespace "mr", keys "cfg", "id")
//
// REALITY SPLIT: this compiles under both board envs here; the actual flash read/write + the wear is
// BENCH-VERIFIED BY THE USER (I cannot exercise on-chip flash from the host). A failed/empty load just
// falls back to the compile-time defaults, so an unprovisioned or mismatched-version chip still boots.
#pragma once
#include <stdint.h>
#include "fault_log.h"   // mrfault::FaultLog — the /mrfault store is whole-blob R/W, exactly like Blob

namespace mrnv {

struct Blob {                  // packed-ish POD; written/read verbatim. Bump kVersion on any layout change.
    uint32_t magic;            // kMagic — distinguishes a real blob from erased flash
    uint16_t version;          // kVersion — a mismatch => ignore (use defaults)
    uint8_t  claim_epoch;      // node_id DAD: the static tiebreak key, persisted so a reboot keeps its seniority (was _pad hi)
    uint8_t  joined;           // node_id DAD: 1 = node_id was DAD-adopted (defends + yields) vs cfg-pinned (was _pad lo)
    uint16_t channel_ctr;      // v15: the self-keyed channel send-ctr — persisted so a reboot CONTINUES (no id-reuse -> no `already-buffered` dedup-drop)
    double   freq_mhz;
    uint32_t bw_hz;
    uint32_t beacon_ms;
    double   duty;
    uint16_t allowed_sf_bitmap;  // the sf_list (bit=sf); 0 = no data SF -> node refuses to originate data
    uint8_t  routing_sf;
    uint8_t  cr;
    uint8_t  lbt;
    uint8_t  node_id;          // 0 = unprovisioned (no sends until join / `cfg set node_id`)
    int8_t   tx_power;         // dBm (repurposed _pad2); SX1262 range -9..22. `cfg set tx_power`
    uint8_t  is_gateway;       // v6: role/topology config (was live-only; now persisted across reboot)
    uint8_t  gateway_only;     // v6: §7 pure-bridge flag (channel-plane consumer half off too)
    uint8_t  is_mobile;        // v6
    uint8_t  leaf_id;          // v6: leaf membership (floods are leaf-scoped)
    uint8_t  ble_mode;         // v7: BLE companion policy — 0=off (bare-metal, default), 1=on, 2=periodic
    uint8_t  ble_period_min;   // v7: periodic-mode advertising period (minutes); reboot-to-apply
    uint32_t ble_pin;          // v7: the 6-digit BLE pairing passkey (0..999999)
    // v8: DUAL-LAYER GATEWAY. n_layers=1 => single-layer (layer 0 = the legacy fields above: node_id / routing_sf /
    // allowed_sf_bitmap / beacon_ms; layer0_id holds the full 8-bit layer_id). n_layers=2 => gateway, layer 1 in the
    // l1_* block. window fields persist the schedule (0 = re-derive the SF-weighted anti-phase split at on_init).
    uint8_t  n_layers;            // 1 or 2 (0/old blob => treated as 1)
    uint8_t  layer0_id;           // layer 0 FULL 8-bit layer_id (single-layer: == leaf_id)
    uint32_t window_period_ms;    // the shared layer0<->layer1 cycle
    uint32_t l0_window_ms;        // layer 0 presence; 0 = derive
    uint32_t l0_window_offset_ms; // layer 0 phase; 0 = derive (layer 0 = 0)
    uint8_t  l1_node_id;          // layer 1 per-leaf node_id (static; live DAD deferred)
    uint8_t  l1_layer_id;         // layer 1 FULL 8-bit layer_id
    uint8_t  l1_claim_epoch;      // layer 1 per-leaf DAD seniority (forward-compat; DAD deferred)
    uint8_t  l1_joined;           // layer 1 per-leaf DAD adopted (forward-compat; DAD deferred)
    uint8_t  l1_routing_sf;       // layer 1 routing SF (5..12)
    uint16_t l1_allowed_sf_bitmap;// layer 1 data-SF set
    uint32_t l1_beacon_period_ms; // layer 1 beacon cadence
    uint32_t l1_window_ms;        // layer 1 presence; 0 = derive
    uint32_t l1_window_offset_ms; // layer 1 phase; 0 = derive anti-phase
    uint8_t  loc_in_dm;           // v9: 1 = piggyback the node's location (DATA_FLAG_LOCATION) on originated DMs.
                                  //     The lat/lon themselves live in /mrid (IdBlob); this is just the opt-in toggle.
    uint8_t  e2e_dm;              // v10: 1 = originate app DMs ENCRYPTED (E2E §4b). Default off -> plaintext (s18-identical).
    // v11: gateway noise control (duty-cycle protection). A gateway is reactive-only in steady state; these gate its
    // sole unsolicited heartbeat. 0 => use the NodeConfig default (5% / 3 h) at boot (an old/zeroed blob stays sane).
    uint8_t  gw_announce_duty_pct;       // v11: % OF the duty budget below which an unsolicited announce is allowed
    uint32_t gw_announce_min_interval_ms;// v11: min ms between unsolicited steady-state announcements
    double   l1_freq_mhz;                // v12: layer-1 RF carrier (per-layer freq). 0 = inherit freq_mhz (layer 0's). Layer 0 reuses freq_mhz.
    uint8_t  gw_herd_slack;             // v13: §3e herd-spread slack factor (spread = exchange_airtime × this). 0 = use the NodeConfig default (2).
    // v14: R6.1 leaf-config membership. lineage_id 0 = UNMANAGED leaf (a pre-v14/never-`leaf create`d node falls here ->
    // backward-compat peering). config_epoch is the LWW config version. leaf_name is in the config_hash (a change re-fingerprints).
    uint16_t lineage_id;                 // v14: operator-minted leaf lineage (0 = unmanaged); u16 (2026-06-20b right-size)
    uint16_t config_epoch;              // v14: monotonic config version
    uint8_t  leaf_name_len;            // v14: 0..meshroute::protocol::leaf_name_max
    uint8_t  leaf_name[16];            // v14: leaf_name bytes (cap = leaf_name_max = 16)
};
constexpr uint32_t kMagic   = 0x4D524331u;   // 'MRC1'
constexpr uint16_t kVersion = 15;            // v15: channel_ctr persist (reboot id-reuse fix). v14: R6.1 leaf-config (lineage_id + config_epoch + leaf_name). v13: gw_herd_slack. v12: per-layer frequency (l1_freq_mhz). v11: gateway-announce duty knobs. v10: e2e_dm toggle. v9: loc_in_dm toggle. v8: DUAL-LAYER GATEWAY (n_layers + layer0_id + window schedule + the l1_*
                                             // block). v7: BLE companion policy. v6: role/topology (is_gateway/...). The Blob
                                             // grew, so every pre-v8 blob fails the `n == sizeof(out)` size check in load()
                                             // and is rejected -> the node re-provisions from defaults (BOTH boards — the
                                             // Blob is shared with the ESP32/Preferences backend; no migration, re-run `cfg set`).

// ---- Identity record (`/mrid`) — SEPARATE from the config blob above (spec §1.4). The 32-byte master
// seed is the single source of truth: ed_pub / key_hash32 / x_* are DERIVED at boot via identity_from_seed
// (so a stored pubkey can never disagree with the seed — a deliberate simplification of §1.4's "store
// ed_pub too"). HW-RNG fills the seed on first boot; `regen` mints a new one; `cfg set name` sets name.
struct IdBlob {
    uint32_t magic;            // kIdMagic — real record vs erased flash
    uint16_t version;          // kIdVersion
    uint16_t name_len;         // bytes of `name` in use (0..sizeof(name))
    uint8_t  seed[32];         // master identity secret (the ONLY persisted key material)
    char     name[32];         // human label (app-level, §1.3); not necessarily null-terminated — name_len bounds it
    // Node location (deployment metadata), degrees × 1e7; (0,0) = unset. APPENDED to /mrid (set once via
    // `cfg set lat`/`lon` or the app). The strict size check below means a legacy /mrid (no lat/lon) is
    // rejected on the first boot after reflashing -> the node re-mints a fresh identity. Fine: dev system.
    int32_t  lat_e7;
    int32_t  lon_e7;
};
constexpr uint32_t kIdMagic   = 0x4D524944u; // 'MRID'
constexpr uint16_t kIdVersion = 1;

// ---- Pinned peer-key store (`/mrpeers`) — E2E §2. The QR/`peerkey`-installed VERIFIED keys, reloaded at boot as
// PINNED so a scanned contact survives reboot with no re-scan. On-air (TOFU) keys stay RAM-only. Whole-blob R/W like
// /mrid; a `peerkey` install rewrites it. Dev hardware: a format change just bumps kPeersVersion (no migration).
struct PeerRec  { uint32_t key_hash32; uint8_t ed_pub[32]; };
struct PeerBlob {
    uint32_t magic;       // kPeersMagic
    uint16_t version;     // kPeersVersion
    uint16_t count;       // entries in use (0..kMaxPinnedPeers)
    PeerRec  rec[16];     // == cap_peer_keys; PINNED keys only
};
constexpr uint32_t kPeersMagic     = 0x4D525052u;  // 'MRPR'
constexpr uint16_t kPeersVersion   = 1;
constexpr uint8_t  kMaxPinnedPeers = 16;

}  // namespace mrnv

#if defined(ARDUINO)
// ----- platform flash backends (header-inline; device_nv.h is included by the one device TU) ----------
#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA) || defined(BOARD_XIAO_WIO_SX1262)
  #include <Adafruit_LittleFS.h>
  #include <InternalFileSystem.h>
namespace mrnv {
inline bool load(Blob& out) {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    File f(InternalFS);
    if (!f.open("/mrcfg", FILE_O_READ)) return false;
    const int n = f.read(reinterpret_cast<uint8_t*>(&out), sizeof(out));
    f.close();
    return n == static_cast<int>(sizeof(out)) && out.magic == kMagic && (out.version >= 2 && out.version <= kVersion);
}
inline bool save(const Blob& b) {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    InternalFS.remove("/mrcfg");                       // overwrite (LittleFS append-only otherwise)
    File f(InternalFS);
    if (!f.open("/mrcfg", FILE_O_WRITE)) return false;
    const int n = f.write(reinterpret_cast<const uint8_t*>(&b), sizeof(b));
    f.close();
    return n == static_cast<int>(sizeof(b));
}
inline bool load_id(IdBlob& out) {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    File f(InternalFS);
    if (!f.open("/mrid", FILE_O_READ)) return false;
    const int n = f.read(reinterpret_cast<uint8_t*>(&out), sizeof(out));
    f.close();
    return n == static_cast<int>(sizeof(out)) && out.magic == kIdMagic && out.version == kIdVersion;
}
inline bool save_id(const IdBlob& b) {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    InternalFS.remove("/mrid");
    File f(InternalFS);
    if (!f.open("/mrid", FILE_O_WRITE)) return false;
    const int n = f.write(reinterpret_cast<const uint8_t*>(&b), sizeof(b));
    f.close();
    return n == static_cast<int>(sizeof(b));
}
inline bool load_peers(PeerBlob& out) {                // §2: the pinned-key store
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    File f(InternalFS);
    if (!f.open("/mrpeers", FILE_O_READ)) return false;
    const int n = f.read(reinterpret_cast<uint8_t*>(&out), sizeof(out));
    f.close();
    return n == static_cast<int>(sizeof(out)) && out.magic == kPeersMagic && out.version == kPeersVersion;
}
inline bool save_peers(const PeerBlob& b) {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    InternalFS.remove("/mrpeers");
    File f(InternalFS);
    if (!f.open("/mrpeers", FILE_O_WRITE)) return false;
    const int n = f.write(reinterpret_cast<const uint8_t*>(&b), sizeof(b));
    f.close();
    return n == static_cast<int>(sizeof(b));
}
// `factory_reset confirm`: erase EVERY persisted NV slot -> the node boots brand-new (default config, fresh
// identity, no peers, empty inbox). TARGETED removal of the known files (NOT InternalFS.format()) so it can't
// nuke unrelated FS state (e.g. OTA). This wipes the InternalFS slots — config + identity + peers + the inbox
// META (/mri_dm, /mri_ch, the next_seq/epoch). The inbox RECORDS live on the SEPARATE external QSPI chip (a
// different FS), so they are wiped by the inbox stores' wipe() in the `factory_reset confirm` command (their
// domain) — together that leaves the inbox truly empty. Best-effort: a remove() of a never-written file no-ops;
// the next boot re-defaults any blob whose magic/version no longer validates regardless.
inline bool factory_erase() {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    InternalFS.remove("/mrcfg");                            // config + node_id
    InternalFS.remove("/mrid");                             // identity master seed (a fresh key/address is minted on boot)
    InternalFS.remove("/mrpeers");                          // pinned peer keys
    InternalFS.remove("/mri_dm");                           // inbox DM meta (next_seq / epoch / read cursor)
    InternalFS.remove("/mri_ch");                           // inbox channel meta  (the QSPI records: command -> store.wipe())
    return true;                                            // best-effort; load-time magic/version re-defaults anything left
    // NB: /mrfault is DELIBERATELY left — the fault history is HW diagnostic, orthogonal to a config/identity reset.
}
// Persistent fault log (`/mrfault`) — whole-blob R/W like Blob (a kFaultVersion bump rejects an old record -> init fresh).
inline bool load_faults(mrfault::FaultLog& out) {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    File f(InternalFS);
    if (!f.open("/mrfault", FILE_O_READ)) return false;
    const int n = f.read(reinterpret_cast<uint8_t*>(&out), sizeof(out));
    f.close();
    return n == static_cast<int>(sizeof(out)) && mrfault::fault_log_valid(out);
}
inline bool save_faults(const mrfault::FaultLog& b) {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    InternalFS.remove("/mrfault");
    File f(InternalFS);
    if (!f.open("/mrfault", FILE_O_WRITE)) return false;
    const int n = f.write(reinterpret_cast<const uint8_t*>(&b), sizeof(b));
    f.close();
    return n == static_cast<int>(sizeof(b));
}
// InternalFS self-heal (Part 2, 2026-06-24): mount + REPAIR-ON-CORRUPT. Returns true IFF it had to reformat (the
// caller logs loudly + sets a flag so `faults`/`version`/`status` surface it this boot). With LFS_NO_ASSERT (Part
// 1) a corrupt CTZ block now RETURNS an error instead of assert()-halting, so we can DETECT it — begin() fails, or
// a known file opens but read()s a negative (a corrupt skip-list head) — and RECOVER via InternalFS.format() -> a
// clean FS that boots. Call ONCE at the very top of setup(), before any load*(). ⚠ Cost: a reformat wipes /mrid too
// -> the node re-mints its identity + loses its join -> must be re-provisioned (a corrupt FS makes even /mrid
// suspect; identity-preservation across a corrupt-format is a flagged later refinement). This is ALSO the recovery
// image for an already-bricked node: it boots, sees the corruption, reformats, comes up clean.
inline bool mount_or_repair() {
    using namespace Adafruit_LittleFS_Namespace;
    bool corrupt = !InternalFS.begin();                         // FS-metadata corruption -> begin() fails
    if (!corrupt) {
        static const char* const kFiles[] = { "/mrcfg", "/mrid", "/mrpeers", "/mri_dm", "/mri_ch", "/mrfault" };
        for (const char* path : kFiles) {
            File f(InternalFS);
            if (f.open(path, FILE_O_READ)) {                    // exists -> probe one read; an absent file (open false) is FINE
                uint8_t b; const int r = f.read(&b, 1);
                f.close();
                if (r < 0) { corrupt = true; break; }           // a read ERROR (LFS_ERR_CORRUPT; not EOF=0) -> corrupt block
            }
        }
    }
    if (corrupt) {
        InternalFS.format();                                    // wipe to a clean FS (all NV slots gone -> defaults + re-mint)
        InternalFS.begin();                                     // re-mount the clean FS so the load*() below run normally
    }
    return corrupt;
}
}  // namespace mrnv
#elif defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3)
  #include <Preferences.h>
namespace mrnv {
inline bool load(Blob& out) {
    Preferences p;
    if (!p.begin("mr", /*readOnly=*/true)) return false;
    const size_t n = p.getBytes("cfg", &out, sizeof(out));
    p.end();
    return n == sizeof(out) && out.magic == kMagic && (out.version >= 2 && out.version <= kVersion);
}
inline bool save(const Blob& b) {
    Preferences p;
    if (!p.begin("mr", /*readOnly=*/false)) return false;
    const size_t n = p.putBytes("cfg", &b, sizeof(b));
    p.end();
    return n == sizeof(b);
}
inline bool load_id(IdBlob& out) {
    Preferences p;
    if (!p.begin("mr", /*readOnly=*/true)) return false;
    const size_t n = p.getBytes("id", &out, sizeof(out));
    p.end();
    return n == sizeof(out) && out.magic == kIdMagic && out.version == kIdVersion;
}
inline bool save_id(const IdBlob& b) {
    Preferences p;
    if (!p.begin("mr", /*readOnly=*/false)) return false;
    const size_t n = p.putBytes("id", &b, sizeof(b));
    p.end();
    return n == sizeof(b);
}
inline bool load_peers(PeerBlob& out) {                // §2: the pinned-key store
    Preferences p;
    if (!p.begin("mr", /*readOnly=*/true)) return false;
    if (!p.isKey("peers")) { p.end(); return false; }   // no peers yet (first boot) — silent, no NVS error
    const size_t n = p.getBytes("peers", &out, sizeof(out));
    p.end();
    return n == sizeof(out) && out.magic == kPeersMagic && out.version == kPeersVersion;
}
inline bool save_peers(const PeerBlob& b) {
    Preferences p;
    if (!p.begin("mr", /*readOnly=*/false)) return false;
    const size_t n = p.putBytes("peers", &b, sizeof(b));
    p.end();
    return n == sizeof(b);
}
// `factory_reset confirm`: erase ALL persisted NV. Config + identity + peers all live as keys in the single
// "mr" Preferences/NVS namespace -> clear() wipes them in one shot (NOT a full nvs_flash_erase, so other
// partitions/OTA state are untouched). The inbox records backend is the [BENCH-TODO] stub here (disabled),
// so there is no separate inbox store to wipe.
inline bool factory_erase() {
    Preferences p;
    if (!p.begin("mr", /*readOnly=*/false)) return false;
    const bool ok = p.clear();                         // wipe the whole "mr" namespace (config + id + peers)
    p.end();
    return ok;
}
// Persistent fault log on ESP32 — its OWN NVS namespace ("mrfault"), so factory_erase()'s clear of "mr" leaves the
// HW fault history intact (consistent with nRF52 keeping /mrfault). A kFaultVersion bump rejects an old record.
inline bool load_faults(mrfault::FaultLog& out) {
    Preferences p;
    if (!p.begin("mrfault", /*readOnly=*/true)) return false;
    if (!p.isKey("log")) { p.end(); return false; }      // no record yet (first boot) — silent, not an NVS error
    const size_t n = p.getBytes("log", &out, sizeof(out));
    p.end();
    return n == sizeof(out) && mrfault::fault_log_valid(out);
}
inline bool save_faults(const mrfault::FaultLog& b) {
    Preferences p;
    if (!p.begin("mrfault", /*readOnly=*/false)) return false;
    const size_t n = p.putBytes("log", &b, sizeof(b));
    p.end();
    return n == sizeof(b);
}
inline bool mount_or_repair() { return false; }    // NVS has no LittleFS-CTZ corruption mode -> nothing to repair (begin() is per-call above)
}  // namespace mrnv
#else
namespace mrnv {                                       // unknown platform -> no NV (always defaults)
inline bool load(Blob&) { return false; }
inline bool save(const Blob&) { return false; }
inline bool load_id(IdBlob&) { return false; }
inline bool save_id(const IdBlob&) { return false; }
inline bool load_peers(PeerBlob&) { return false; }
inline bool save_peers(const PeerBlob&) { return false; }
inline bool factory_erase() { return true; }          // §2 native/unknown no-op stub (device-less build still compiles)
inline bool load_faults(mrfault::FaultLog&) { return false; }
inline bool save_faults(const mrfault::FaultLog&) { return false; }
inline bool mount_or_repair() { return false; }       // native/unknown: no FS
}  // namespace mrnv
#endif
#endif  // ARDUINO
