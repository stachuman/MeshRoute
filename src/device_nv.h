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
    uint16_t allowed_sf_bitmap;
    uint8_t  routing_sf;
    uint8_t  data_sf;
    uint8_t  cr;
    uint8_t  lbt;
    uint8_t  node_id;          // 0 = unprovisioned (no sends until join / `cfg set node_id`)
    int8_t   tx_power;         // dBm (repurposed _pad2); SX1262 range -9..22. `cfg set tx_power`
};
constexpr uint32_t kMagic   = 0x4D524331u;   // 'MRC1'
constexpr uint16_t kVersion = 4;             // v4: claim_epoch + joined repurpose the old _pad (SAME size, so v2/v3
                                             // blobs still parse — their _pad was 0 -> claim_epoch=0, joined=0).
                                             // tx_power (was _pad2); load() ALSO accepts v2 (identical
                                             // layout) + defaults tx_power, so the bump preserves config

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
