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

namespace mrnv {

struct Blob {                  // packed-ish POD; written/read verbatim. Bump kVersion on any layout change.
    uint32_t magic;            // kMagic — distinguishes a real blob from erased flash
    uint16_t version;          // kVersion — a mismatch => ignore (use defaults)
    uint8_t  claim_epoch;      // node_id DAD: the static tiebreak key, persisted so a reboot keeps its seniority (was _pad hi)
    uint8_t  joined;           // node_id DAD: 1 = node_id was DAD-adopted (defends + yields) vs cfg-pinned (was _pad lo)
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
};
constexpr uint32_t kMagic   = 0x4D524331u;   // 'MRC1'
constexpr uint16_t kVersion = 8;             // v8: DUAL-LAYER GATEWAY (n_layers + layer0_id + window schedule + the l1_*
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
}  // namespace mrnv
#else
namespace mrnv {                                       // unknown platform -> no NV (always defaults)
inline bool load(Blob&) { return false; }
inline bool save(const Blob&) { return false; }
inline bool load_id(IdBlob&) { return false; }
inline bool save_id(const IdBlob&) { return false; }
}  // namespace mrnv
#endif
#endif  // ARDUINO
